#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "wake_word.h"

// Раз на REC_PERIOD_S секунд знімок 1-секундного вікна виводиться в serial
// (hex у рядках "R <hex>") для відновлення в WAV скриптом tools/serial_to_wav.py
#define REC_ENABLED 1
#define REC_PERIOD_S 30

esp_err_t recorder_init(void);

// Викликається з inference_task: якщо минув період і попередній дамп завершено,
// копіює вікно й будить recorder_task
void recorder_capture(const int16_t *window);

// Task: виводить знімок у serial (повільно, тому окремо від inference_task)
void recorder_task(void *pvParameters);
