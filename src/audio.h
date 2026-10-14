#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// I2S мікрофон (INMP441 / SPH0645 і подібні)
#define MIC_I2S_BCLK GPIO_NUM_4
#define MIC_I2S_WS GPIO_NUM_5
#define MIC_I2S_DIN GPIO_NUM_6

#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_FRAME_SAMPLES 512 // 32 мс

typedef struct
{
	int16_t samples[AUDIO_FRAME_SAMPLES];
} audio_frame_t;

esp_err_t audio_init(void);

// Task: читає I2S і кладе кадри в audio_queue
void audio_task(void *pvParameters);

extern QueueHandle_t audio_queue;
