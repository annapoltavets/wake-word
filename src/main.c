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

#define HOP_FRAMES 4	   // інференс кожні 4 * 32 мс = 128 мс
#define COOLDOWN_FRAMES 31 // ~1 с паузи після спрацювання

// Пропуск тиші: модель запускається, лише якщо в вікні є кадр гучніший за поріг.
// Шкала після >> 16 у audio.c: фон ~30-250, мова ~500-5000. 0 вимикає перевірку.
// (Запуск за подією/VAD по кінцю слова не спрацював: у шумі кінець слова не знаходиться,
// а запуск раз на ~1 с дає випадкове положення слова у вікні.)
#ifndef VAD_RMS_MIN
#define VAD_RMS_MIN 250.0f
#endif
#define WINDOW_FRAMES (WW_WINDOW_SAMPLES / AUDIO_FRAME_SAMPLES) // кадрів у вікні (31)

static int16_t window[WW_WINDOW_SAMPLES];
static float frame_rms[WINDOW_FRAMES]; // кільце RMS останніх кадрів

static float rms_of(const int16_t *samples, size_t n)
{
	float acc = 0.0f;
	for (size_t i = 0; i < n; i++)
	{
		acc += (float)samples[i] * samples[i];
	}
	return sqrtf(acc / n);
}

// Task: збирає ковзне вікно 1 с, запускає модель, змінює стан діода під м'ютексом
static void inference_task(void *pvParameters)
{
	audio_frame_t frame;
	ww_result_t result;
	uint32_t frames = 0;
	uint32_t cooldown = 0;
	uint32_t frame_idx = 0;
	uint32_t skipped = 0;
	int64_t infer_last_us = 0;
	int64_t infer_max_us = 0;
	int64_t next_status_us = 0;

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

		frame_rms[frame_idx] = rms_of(frame.samples, AUDIO_FRAME_SAMPLES);
		frame_idx = (frame_idx + 1) % WINDOW_FRAMES;

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
			continue;
		}
		if (++frames < HOP_FRAMES)
		{
			continue;
		}
		frames = 0;

		// тиша або тихий шум у всьому вікні: інференс не потрібен
		float loudest = 0.0f;
		for (size_t i = 0; i < WINDOW_FRAMES; i++)
		{
			if (frame_rms[i] > loudest)
			{
				loudest = frame_rms[i];
			}
		}
		if (loudest < VAD_RMS_MIN)
		{
			if (++skipped % 64 == 1)
			{
				ESP_LOGI(TAG, "silence (rms %.0f < %.0f), inference skipped x%lu",
						 loudest, VAD_RMS_MIN, (unsigned long)skipped);
			}
			continue;
		}

		int64_t t0 = esp_timer_get_time();
		wake_word_process(window, &result);
		infer_last_us = esp_timer_get_time() - t0;
		if (infer_last_us > infer_max_us)
		{
			infer_max_us = infer_last_us;
		}
		ESP_LOGI(TAG, "inference label=%d score=%.2f (rms %.0f) took=%d ms lag=%d ms queue=%u/%d",
				 result.label, result.score, loudest, (int)(infer_last_us / 1000),
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
			led_set_state(LED_CMD_ON, frame.t_us);
			break;
		case WW_OFF:
			led_set_state(LED_CMD_OFF, frame.t_us);
			break;
		case WW_TOGGLE:
			led_set_state(LED_CMD_TOGGLE, frame.t_us);
			break;
		case WW_BLINK:
			led_set_state(LED_CMD_BLINK, frame.t_us);
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
