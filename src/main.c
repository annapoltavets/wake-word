#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "audio.h"
#include "led.h"
#include "recorder.h"
#include "wake_word.h"

static const char *TAG = "MAIN";

#define COOLDOWN_FRAMES 31 // ~1 с паузи після спрацювання

// Запуск моделі за подією (VAD по енергії), а не за таймером: слово виявляється як сплеск
// гучності, модель запускається один раз після його закінчення на вікні 1 с, де слово по центру.
// Шкала після >> 16 у audio.c: фон ~30-150, мова ~1000-5000.
#ifndef VAD_RMS_MIN
#define VAD_RMS_MIN 250.0f // кадр гучніший за це - початок слова
#endif
#define VAD_RMS_KEEP_RATIO 0.6f // всередині слова поріг нижчий (гістерезис)
#define VAD_ON_FRAMES 2			// стільки гучних кадрів підряд = початок слова (64 мс)
#define VAD_HANG_FRAMES 5		// стільки тихих кадрів підряд = кінець слова (160 мс)
#define VAD_MAX_ACTIVE_FRAMES 22 // обмеження на довгий шум/мову (~700 мс), далі примусовий запуск
#define REFRACTORY_FRAMES 6		// пауза після запуску, щоб хвіст слова не спрацював знову
#define WINDOW_FRAMES (WW_WINDOW_SAMPLES / AUDIO_FRAME_SAMPLES) // кадрів у вікні (31)

typedef enum
{
	VAD_IDLE,  // тиша, чекаємо початок слова
	VAD_ACTIVE, // слово триває
	VAD_POST   // слово закінчилось, добираємо звук після нього, щоб слово було по центру вікна
} vad_state_t;

static int16_t window[WW_WINDOW_SAMPLES];

static float rms_of(const int16_t *samples, size_t n)
{
	float acc = 0.0f;
	for (size_t i = 0; i < n; i++)
	{
		acc += (float)samples[i] * samples[i];
	}
	return sqrtf(acc / n);
}

// Task: збирає ковзне вікно 1 с, за сплеском гучності запускає модель, змінює стан діода під м'ютексом
static void inference_task(void *pvParameters)
{
	audio_frame_t frame;
	ww_result_t result;
	uint32_t cooldown = 0;
	uint32_t refractory = 0;
	int64_t infer_last_us = 0;
	int64_t infer_max_us = 0;
	int64_t next_status_us = 0;

	vad_state_t state = VAD_IDLE;
	uint32_t loud_run = 0;		// гучних кадрів підряд (у IDLE)
	uint32_t active_frames = 0; // кадрів від початку слова
	uint32_t quiet_run = 0;		// тихих кадрів підряд (у ACTIVE)
	uint32_t post_left = 0;		// скільки кадрів ще добрати після слова
	uint32_t word_frames = 0;
	float word_peak = 0.0f;		// найгучніший кадр слова
	int64_t word_end_us = 0;	// час останнього гучного кадру (для метрики мікрофон -> діод)

	for (;;)
	{
		if (xQueueReceive(audio_queue, &frame, portMAX_DELAY) != pdTRUE)
		{
			continue;
		}

		// зсунути вікно вліво й дописати новий кадр
		memmove(window, window + AUDIO_FRAME_SAMPLES,
				(WW_WINDOW_SAMPLES - AUDIO_FRAME_SAMPLES) * sizeof(int16_t));
		memcpy(window + WW_WINDOW_SAMPLES - AUDIO_FRAME_SAMPLES, frame.samples, sizeof(frame.samples));

		recorder_capture(window);

		// діагностика затримки: раз на ~10 с
		int64_t now_us = esp_timer_get_time();
		if (now_us >= next_status_us)
		{
			next_status_us = now_us + 10000000;
			ESP_LOGI(TAG, "status lag=%d ms queue=%u/%d infer last=%d max=%d ms heap=%u min_heap=%u stack_free=%u B",
					 (int)((now_us - frame.t_us) / 1000),
					 (unsigned)uxQueueMessagesWaiting(audio_queue), AUDIO_QUEUE_DEPTH,
					 (int)(infer_last_us / 1000), (int)(infer_max_us / 1000),
					 (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
					 (unsigned)uxTaskGetStackHighWaterMark(NULL));
		}

		if (cooldown > 0)
		{
			cooldown--;
			state = VAD_IDLE;
			loud_run = 0;
			continue;
		}

		float rms = rms_of(frame.samples, AUDIO_FRAME_SAMPLES);
		bool fire = false;

		switch (state)
		{
		case VAD_IDLE:
			if (refractory > 0)
			{
				refractory--;
			}
			else if (rms >= VAD_RMS_MIN)
			{
				if (++loud_run >= VAD_ON_FRAMES)
				{
					state = VAD_ACTIVE;
					active_frames = loud_run;
					quiet_run = 0;
					word_peak = rms;
					word_end_us = frame.t_us;
				}
			}
			else
			{
				loud_run = 0;
			}
			break;

		case VAD_ACTIVE:
			active_frames++;
			if (rms >= VAD_RMS_MIN * VAD_RMS_KEEP_RATIO)
			{
				quiet_run = 0;
				word_end_us = frame.t_us;
				if (rms > word_peak)
				{
					word_peak = rms;
				}
			}
			else
			{
				quiet_run++;
			}

			if (quiet_run >= VAD_HANG_FRAMES || active_frames >= VAD_MAX_ACTIVE_FRAMES)
			{
				// слово по центру вікна: після нього має лишитися (вікно - слово) / 2 кадрів
				word_frames = active_frames - quiet_run;
				uint32_t want_after = word_frames < WINDOW_FRAMES ? (WINDOW_FRAMES - word_frames) / 2 : 0;
				post_left = want_after > quiet_run ? want_after - quiet_run : 0;
				state = VAD_POST;
				fire = post_left == 0;
			}
			break;

		case VAD_POST:
			fire = --post_left == 0;
			break;
		}

		if (!fire)
		{
			continue;
		}

		state = VAD_IDLE;
		loud_run = 0;
		refractory = REFRACTORY_FRAMES;

		int64_t t0 = esp_timer_get_time();
		wake_word_process(window, &result);
		infer_last_us = esp_timer_get_time() - t0;
		if (infer_last_us > infer_max_us)
		{
			infer_max_us = infer_last_us;
		}
		ESP_LOGI(TAG, "inference label=%d score=%.2f (word %u frames, peak rms %.0f) took=%d ms lag=%d ms queue=%u/%d",
				 result.label, result.score, (unsigned)word_frames, word_peak, (int)(infer_last_us / 1000),
				 (int)((esp_timer_get_time() - frame.t_us) / 1000),
				 (unsigned)uxQueueMessagesWaiting(audio_queue), AUDIO_QUEUE_DEPTH);
		if (result.label == WW_NONE || result.score < WW_THRESHOLD)
		{
			continue;
		}

		ESP_LOGI(TAG, "detected label=%d score=%.2f", result.label, result.score);
		switch (result.label)
		{
		case WW_ON:
			led_set_state(LED_CMD_ON, word_end_us);
			break;
		case WW_OFF:
			led_set_state(LED_CMD_OFF, word_end_us);
			break;
		case WW_TOGGLE:
			led_set_state(LED_CMD_TOGGLE, word_end_us);
			break;
		case WW_BLINK:
			led_set_state(LED_CMD_BLINK, word_end_us);
			break;
		default:
			break;
		}
		cooldown = COOLDOWN_FRAMES;

		// скинути накопичену чергу: після спрацювання старі кадри (в т.ч. саме слово) не потрібні,
		// а cooldown і так заново наповнить вікно свіжим звуком
		UBaseType_t pending = uxQueueMessagesWaiting(audio_queue);
		xQueueReset(audio_queue);
		ESP_LOGI(TAG, "audio queue cleared (%u frames dropped)", (unsigned)pending);
	}
}
void app_main(void)
{
	led_init();
	ESP_ERROR_CHECK(audio_init());
	ESP_ERROR_CHECK(wake_word_init());
	ESP_ERROR_CHECK(recorder_init());

	xTaskCreatePinnedToCore(led_task, "led_task", 2048, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 6, NULL, 1);
#if REC_ENABLED
	// повільний дамп у serial на ядрі 0 з низьким пріоритетом
	xTaskCreatePinnedToCore(recorder_task, "recorder_task", 4096, NULL, 1, NULL, 0);
#endif
	// важкий інференс на ядрі 0, щоб не заважати читанню I2S
	xTaskCreatePinnedToCore(inference_task, "inference_task", 16384, NULL, 5, NULL, 0);
}
