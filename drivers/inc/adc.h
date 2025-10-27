#pragma once

#include <stdint.h>

// to minimize ADC reading errors, read multiple times and average
// that result. Since the ADC internally averages over 96 samples
// every multiple will result in 96 more readings and slows down
// the aquiring time substancially.
uint16_t adc_read_accurate(uint8_t n_average);
