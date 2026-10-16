#include "recorder.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio.h"

static const char *TAG = "REC";

#define SAMPLES_PER_LINE 64

static int16_t snapshot[WW_WINDOW_SAMPLES];
static TaskHandle_t rec_task_handle;
static volatile bool busy;
static int64_t next_capture_us;

esp_err_t recorder_init(void)
{
	next_capture_us = (int64_t)REC_PERIOD_S * 1000000;
	return ESP_OK;
}

void recorder_capture(const int16_t *window)
{
#if REC_ENABLED
	if (busy || rec_task_handle == NULL || esp_timer_get_time() < next_capture_us)
	{
		return;
	}
	memcpy(snapshot, window, sizeof(snapshot));
	busy = true;
	xTaskNotifyGive(rec_task_handle);
#else
	(void)window;
#endif
}

void recorder_task(void *pvParameters)
{
	static const char hex[] = "0123456789abcdef";
	char line[2 + SAMPLES_PER_LINE * 4 + 2];

	rec_task_handle = xTaskGetCurrentTaskHandle();

	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		ESP_LOGI(TAG, "dumping %d samples, takes ~%d s", WW_WINDOW_SAMPLES,
				 WW_WINDOW_SAMPLES * 2 * 2 * 10 / 115200 + 1);
		printf("REC_BEGIN %d %d\n", WW_WINDOW_SAMPLES, AUDIO_SAMPLE_RATE);

		uint32_t sum = 0;
		for (size_t i = 0; i < WW_WINDOW_SAMPLES; i += SAMPLES_PER_LINE)
		{
			char *p = line;
			*p++ = 'R';
			*p++ = ' ';
			for (size_t j = i; j < i + SAMPLES_PER_LINE && j < WW_WINDOW_SAMPLES; j++)
			{
				uint16_t v = (uint16_t)snapshot[j];
				sum += v;
				*p++ = hex[v >> 12];
				*p++ = hex[(v >> 8) & 0xF];
				*p++ = hex[(v >> 4) & 0xF];
				*p++ = hex[v & 0xF];
			}
			*p++ = '\n';
			*p = '\0';
			// один рядок за раз, щоб логи інших задач не вклинювалися всередину
			fputs(line, stdout);

			// консольний UART чекає в циклі, даємо час IDLE (task watchdog)
			if ((i / SAMPLES_PER_LINE) % 8 == 7)
			{
				vTaskDelay(1);
			}
		}
		printf("REC_END %lu\n", (unsigned long)sum);

		next_capture_us = esp_timer_get_time() + (int64_t)REC_PERIOD_S * 1000000;
		busy = false;
	}
}
