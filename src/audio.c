#include "audio.h"

#include "driver/i2s_std.h"
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "AUDIO";

QueueHandle_t audio_queue;
static i2s_chan_handle_t rx_chan;

esp_err_t audio_init(void)
{
	audio_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(audio_frame_t));
	if (audio_queue == NULL)
	{
		return ESP_ERR_NO_MEM;
	}

	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
	ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

	// INMP441: 24 біти даних у 32-бітному слоті, лівий канал (L/R -> GND)
	i2s_std_config_t std_cfg = {
		.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
		.gpio_cfg = {
			.mclk = I2S_GPIO_UNUSED,
			.bclk = MIC_I2S_BCLK,
			.ws = MIC_I2S_WS,
			.dout = I2S_GPIO_UNUSED,
			.din = MIC_I2S_DIN,
			.invert_flags = {0},
		},
	};
	std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

	ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
	ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

	ESP_LOGI(TAG, "I2S ready, %d Hz", AUDIO_SAMPLE_RATE);
	return ESP_OK;
}

void audio_task(void *pvParameters)
{
	static int32_t raw[AUDIO_FRAME_SAMPLES];
	audio_frame_t frame;
	size_t bytes_read;
	float sum_sq = 0.0f;
	uint32_t sum_n = 0;
	int32_t peak = 0;
	uint32_t frames = 0;

	for (;;)
	{
		if (i2s_channel_read(rx_chan, raw, sizeof(raw), &bytes_read, portMAX_DELAY) != ESP_OK)
		{
			continue;
		}

		frame.t_us = esp_timer_get_time();
		size_t n = bytes_read / sizeof(int32_t);
		for (size_t i = 0; i < n; i++)
		{
			// 32 біти -> 16 біт (корисні 24 біти, зсув 16 без підсилення; 14 давало кліпінг мови)
			int32_t s = raw[i] >> 16;
			if (s > INT16_MAX)
				s = INT16_MAX;
			if (s < INT16_MIN)
				s = INT16_MIN;
			frame.samples[i] = (int16_t)s;

			sum_sq += (float)s * s;
			int32_t a = s < 0 ? -s : s;
			if (a > peak)
				peak = a;
		}
		sum_n += n;

		// рівень мікрофона раз на ~1 с (31 кадр по 32 мс)
		if (++frames >= 31)
		{
			ESP_LOGI(TAG, "mic rms=%.0f peak=%ld", sum_n ? sqrtf(sum_sq / sum_n) : 0.0f, (long)peak);
			sum_sq = 0.0f;
			sum_n = 0;
			peak = 0;
			frames = 0;
		}

		if (n == AUDIO_FRAME_SAMPLES && xQueueSend(audio_queue, &frame, 0) != pdTRUE)
		{
			ESP_LOGW(TAG, "audio queue full, frame dropped");
		}
	}
}
