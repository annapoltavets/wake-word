#include "led.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "LED";

static SemaphoreHandle_t led_mutex;
static TaskHandle_t led_task_handle;
static uint32_t led_level; // спільний стан, захищений led_mutex
static bool blink_pending; // запит на блимання, теж під led_mutex
static int64_t cmd_mic_t_us; // час кадру мікрофона останньої команди (під led_mutex)
static int64_t cmd_notify_t_us; // час виклику led_set_state (під led_mutex)

void led_init(void)
{
	gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(LED_GPIO, 0);
	led_mutex = xSemaphoreCreateMutex();
	configASSERT(led_mutex != NULL);
}

void led_set_state(led_cmd_t cmd, int64_t mic_t_us)
{
	xSemaphoreTake(led_mutex, portMAX_DELAY);
	cmd_mic_t_us = mic_t_us;
	cmd_notify_t_us = esp_timer_get_time();
	switch (cmd)
	{
	case LED_CMD_ON:
		led_level = 1;
		break;
	case LED_CMD_OFF:
		led_level = 0;
		break;
	case LED_CMD_TOGGLE:
		led_level = !led_level;
		break;
	case LED_CMD_BLINK:
		blink_pending = true;
		break;
	}
	xSemaphoreGive(led_mutex);

	if (led_task_handle != NULL)
	{
		xTaskNotifyGive(led_task_handle);
	}
}

void led_task(void *pvParameters)
{
	led_task_handle = xTaskGetCurrentTaskHandle();

	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		xSemaphoreTake(led_mutex, portMAX_DELAY);
		uint32_t level = led_level;
		bool blink = blink_pending;
		blink_pending = false;
		int64_t mic_t_us = cmd_mic_t_us;
		int64_t notify_t_us = cmd_notify_t_us;
		xSemaphoreGive(led_mutex);

		// момент, коли діод реально змінюється (для блимання - перший спалах)
		int64_t now_us = esp_timer_get_time();
		ESP_LOGI(TAG, "latency mic->LED=%d ms (inference->LED=%d ms)",
				 (int)((now_us - mic_t_us) / 1000), (int)((now_us - notify_t_us) / 1000));

		if (blink)
		{
			ESP_LOGI(TAG, "LED blink x%d", LED_BLINK_COUNT);
			for (int i = 0; i < LED_BLINK_COUNT; i++)
			{
				gpio_set_level(LED_GPIO, 1);
				vTaskDelay(pdMS_TO_TICKS(LED_BLINK_MS));
				gpio_set_level(LED_GPIO, 0);
				vTaskDelay(pdMS_TO_TICKS(LED_BLINK_MS));
			}
		}

		// повернути збережений стан (після блимання він міг змінитися, тому перечитуємо)
		xSemaphoreTake(led_mutex, portMAX_DELAY);
		level = led_level;
		xSemaphoreGive(led_mutex);

		gpio_set_level(LED_GPIO, level);
		ESP_LOGI(TAG, "LED %s", level ? "ON" : "OFF");
	}
}
