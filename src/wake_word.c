#include "wake_word.h"

#include <math.h>
#include "esp_log.h"
#include "ei_wrapper.h"

static const char *TAG = "WAKE_WORD";

// 1 = заглушка за гучністю (хлопок/голос) для перевірки ланцюга мікрофон -> задачі -> діод,
// 0 = модель Edge Impulse (components/edge_impulse)
#define WW_USE_ENERGY_STUB 0
// Мітка ключового слова в моделі (інша мітка - "unknown")
#define WW_KEYWORD_LABEL "carlos"
#define WW_STUB_RMS_LEVEL 3000.0f

esp_err_t wake_word_init(void)
{
#if WW_USE_ENERGY_STUB
	ESP_LOGW(TAG, "using energy stub");
#else
	if (ei_wrapper_window_samples() != WW_WINDOW_SAMPLES)
	{
		ESP_LOGE(TAG, "model expects %u samples, WW_WINDOW_SAMPLES=%d",
				 (unsigned)ei_wrapper_window_samples(), WW_WINDOW_SAMPLES);
		return ESP_ERR_INVALID_SIZE;
	}
#endif
	return ESP_OK;
}

void wake_word_process(const int16_t *window, ww_result_t *result)
{
	result->label = WW_NONE;
	result->score = 0.0f;

#if WW_USE_ENERGY_STUB
	// RMS тільки останніх 0.25 с вікна
	const size_t n = WW_WINDOW_SAMPLES / 4;
	const int16_t *tail = window + WW_WINDOW_SAMPLES - n;
	float acc = 0.0f;
	for (size_t i = 0; i < n; i++)
	{
		acc += (float)tail[i] * tail[i];
	}
	float rms = sqrtf(acc / n);
	if (rms > WW_STUB_RMS_LEVEL)
	{
		result->label = WW_TOGGLE;
		result->score = 1.0f;
	}
#else
	// єдине ключове слово модель сприймається як перемикач діода
	float score = 0.0f;
	if (ei_wrapper_classify(window, WW_KEYWORD_LABEL, &score) == ESP_OK)
	{
		result->label = WW_TOGGLE;
		result->score = score;
	}
#endif
}
