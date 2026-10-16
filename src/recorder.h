#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "wake_word.h"

// Раз на REC_PERIOD_S секунд знімок 1-секундного вікна виводиться в serial
// (hex у рядках "R <hex>") для відновлення в WAV скриптом tools/serial_to_wav.py
// 1 = увімкнено, 0 = вимкнено (задача не створюється). Можна задати без правки коду:
// build_flags = -DREC_ENABLED=1 у platformio.ini
#ifndef REC_ENABLED
#define REC_ENABLED 0
#endif
#ifndef REC_PERIOD_S
#define REC_PERIOD_S 30
#endif

esp_err_t recorder_init(void);

// Викликається з inference_task: якщо минув період і попередній дамп завершено,
// копіює вікно й будить recorder_task
void recorder_capture(const int16_t *window);

// Task: виводить знімок у serial (повільно, тому окремо від inference_task)
void recorder_task(void *pvParameters);
