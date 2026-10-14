#include "wake_word.h"

#include <math.h>
#include "esp_log.h"

static const char *TAG = "WAKE_WORD";

// TODO: модель ще не підключена. Поки що заглушка за гучністю (хлопок/голос),
// щоб перевірити ланцюг мікрофон -> задачі -> діод. Поставити 0 після підключення моделі.
#define WW_USE_ENERGY_STUB 1
#define WW_STUB_RMS_LEVEL 3000.0f

esp_err_t wake_word_init(void)
{
	// TODO: завантажити модель (напр. TFLite Micro) і виділити tensor arena
	ESP_LOGW(TAG, "model not loaded, using energy stub");
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
	// TODO: features (MFCC / spectrogram) -> інференс -> argmax у result
	(void)window;
#endif
}
