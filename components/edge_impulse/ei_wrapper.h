#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Кількість семплів, яку очікує модель (EI_CLASSIFIER_RAW_SAMPLE_COUNT)
size_t ei_wrapper_window_samples(void);

// Запускає імпульс на вікні int16 і повертає впевненість для мітки label.
// Якщо мітки немає в моделі, повертає ESP_ERR_NOT_FOUND.
esp_err_t ei_wrapper_classify(const int16_t *window, const char *label, float *score);

#ifdef __cplusplus
}
#endif
