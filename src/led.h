#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LED_GPIO GPIO_NUM_15
#define LED_BLINK_COUNT 2
#define LED_BLINK_MS 150 // тривалість увімкнено / вимкнено

typedef enum
{
	LED_CMD_OFF,
	LED_CMD_ON,
	LED_CMD_TOGGLE,
	LED_CMD_BLINK // двічі блимнути, потім повернути попередній стан
} led_cmd_t;

void led_init(void);

// Змінює стан діода під м'ютексом і будить led_task (викликається з inference_task).
// mic_t_us - час (esp_timer) читання кадру з мікрофона, що викликав команду; led_task логує затримку мікрофон -> діод
void led_set_state(led_cmd_t cmd, int64_t mic_t_us);

// Task: чекає сповіщення, читає стан під м'ютексом і вмикає/вимикає діод
void led_task(void *pvParameters);
