#include "ei_wrapper.h"

#include <string.h>

#include "esp_log.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

static const char *TAG = "EI";

static const int16_t *s_window;

static int get_data(size_t offset, size_t length, float *out_ptr)
{
	return ei::numpy::int16_to_float(s_window + offset, out_ptr, length);
}

size_t ei_wrapper_window_samples(void)
{
	return EI_CLASSIFIER_RAW_SAMPLE_COUNT;
}

esp_err_t ei_wrapper_classify(const int16_t *window, const char *label, float *score)
{
	s_window = window;

	signal_t signal;
	signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
	signal.get_data = &get_data;

	ei_impulse_result_t result = {};
	EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
	if (err != EI_IMPULSE_OK)
	{
		ESP_LOGE(TAG, "run_classifier failed (%d)", err);
		return ESP_FAIL;
	}

	for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
	{
		if (strcmp(result.classification[i].label, label) == 0)
		{
			*score = result.classification[i].value;
			return ESP_OK;
		}
	}
	return ESP_ERR_NOT_FOUND;
}
