#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LED_GPIO GPIO_NUM_15

typedef enum
{
	LED_CMD_OFF,
	LED_CMD_ON,
	LED_CMD_TOGGLE
} led_cmd_t;

void led_init(void);

// Змінює стан діода під м'ютексом і будить led_task (викликається з inference_task)
void led_set_state(led_cmd_t cmd);

// Task: чекає сповіщення, читає стан під м'ютексом і вмикає/вимикає діод
void led_task(void *pvParameters);
