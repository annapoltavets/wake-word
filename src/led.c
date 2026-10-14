#include "led.h"

#include "esp_log.h"

static const char *TAG = "LED";

static SemaphoreHandle_t led_mutex;
static TaskHandle_t led_task_handle;
static uint32_t led_level; // спільний стан, захищений led_mutex

void led_init(void)
{
	gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(LED_GPIO, 0);
	led_mutex = xSemaphoreCreateMutex();
	configASSERT(led_mutex != NULL);
}

void led_set_state(led_cmd_t cmd)
{
	xSemaphoreTake(led_mutex, portMAX_DELAY);
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
		xSemaphoreGive(led_mutex);

		gpio_set_level(LED_GPIO, level);
		ESP_LOGI(TAG, "LED %s", level ? "ON" : "OFF");
	}
}
