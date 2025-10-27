#include "adc.h"
#include <hardware/adc.h>

// to minimize ADC reading errors, read multiple times and average
// that result. Since the ADC internally averages over 96 samples
// every multiple will result in 96 more readings and slows down
// the aquiring time substancially.
uint16_t adc_read_accurate(uint8_t n_average)
{
	adc_read(); // first reading is thrown away
	adc_read(); // second reading is thrown away
	adc_read(); // third reading is thrown away
	adc_read(); // fourth reading is thrown away

	uint32_t sum = 0;

	for (size_t i = 0; i < n_average; ++i) {
		sum += adc_read();
	}

	uint16_t result = (uint16_t)(sum / n_average);

	// clamp values and prevent very short spikes
	if (result < 120)
		result = 0;
	if (result > 4095)
		result = 4095;

	return result;
}
