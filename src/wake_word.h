#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Вікно, яке очікує модель: 1 с аудіо 16 кГц mono int16
#define WW_WINDOW_SAMPLES 16000
// Мінімальна впевненість, щоб прийняти результат
#define WW_THRESHOLD 0.8f

typedef enum
{
	WW_NONE,
	WW_ON,
	WW_OFF,
	WW_TOGGLE
} ww_label_t;

typedef struct
{
	ww_label_t label;
	float score;
} ww_result_t;

esp_err_t wake_word_init(void);

// Запускає класифікатор на вікні WW_WINDOW_SAMPLES
void wake_word_process(const int16_t *window, ww_result_t *result);
