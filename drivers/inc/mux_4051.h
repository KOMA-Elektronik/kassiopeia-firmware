#ifndef _MUX_4051_H_
#define _MUX_4051_H_

#include <stdint.h>

typedef enum _Mux4051Channel {
	MUX_4051_CH_0 = 0,
	MUX_4051_CH_1 = 1,
	MUX_4051_CH_2 = 2,
	MUX_4051_CH_3 = 3,
	MUX_4051_CH_4 = 4,
	MUX_4051_CH_5 = 5,
	MUX_4051_CH_6 = 6,
	MUX_4051_CH_7 = 7,
} Mux4051Channel;

typedef struct _Mux4051 {
	int8_t select_pin_a;
	int8_t select_pin_b;
	int8_t select_pin_c;
} Mux4051;

// Select the appropriate pin for the respective select bit. Negative values
// will result in ignoring that internal mux.
Mux4051 Mux4051_init(int8_t pin_a, int8_t pin_b, int8_t pin_c);

// Switches the mux to channel `x`
void switch_to(Mux4051* instance, Mux4051Channel x);

#endif // _MUX_4051_H_