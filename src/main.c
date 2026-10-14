#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio.h"
#include "led.h"
#include "wake_word.h"

static const char *TAG = "MAIN";

#define HOP_FRAMES 8	  // інференс кожні 8 * 32 мс = 256 мс
#define COOLDOWN_FRAMES 31 // ~1 с паузи після спрацювання

static int16_t window[WW_WINDOW_SAMPLES];

// Task: збирає ковзне вікно 1 с, запускає модель, змінює стан діода під м'ютексом
static void inference_task(void *pvParameters)
{
	audio_frame_t frame;
	ww_result_t result;
	uint32_t frames = 0;
	uint32_t cooldown = 0;

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

		wake_word_process(window, &result);
		if (result.label == WW_NONE || result.score < WW_THRESHOLD)
		{
			continue;
		}

		ESP_LOGI(TAG, "detected label=%d score=%.2f", result.label, result.score);
		switch (result.label)
		{
		case WW_ON:
			led_set_state(LED_CMD_ON);
			break;
		case WW_OFF:
			led_set_state(LED_CMD_OFF);
			break;
		case WW_TOGGLE:
			led_set_state(LED_CMD_TOGGLE);
			break;
		default:
			break;
		}
		cooldown = COOLDOWN_FRAMES;
	}
}

void app_main(void)
{
	led_init();
	ESP_ERROR_CHECK(audio_init());
	ESP_ERROR_CHECK(wake_word_init());

	xTaskCreatePinnedToCore(led_task, "led_task", 2048, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 6, NULL, 1);
	// важкий інференс на ядрі 0, щоб не заважати читанню I2S
	xTaskCreatePinnedToCore(inference_task, "inference_task", 8192, NULL, 5, NULL, 0);
}
