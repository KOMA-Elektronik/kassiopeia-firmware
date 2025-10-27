#include "mux_4051.h"

#include <pico/stdlib.h>

//////////////////////////////////////
// Public
//////////////////////////////////////

Mux4051 Mux4051_init(int8_t pin_a, int8_t pin_b, int8_t pin_c)
{
	Mux4051 mux;

	mux.select_pin_a = pin_a;
	gpio_set_function(pin_a, GPIO_FUNC_SIO);
	gpio_set_dir(pin_a, GPIO_OUT);

	mux.select_pin_b = pin_b;
	gpio_set_function(pin_b, GPIO_FUNC_SIO);
	gpio_set_dir(pin_b, GPIO_OUT);

	mux.select_pin_c = pin_c;
	gpio_set_function(pin_c, GPIO_FUNC_SIO);
	gpio_set_dir(pin_c, GPIO_OUT);

	return mux;
}

void switch_to(Mux4051* instance, Mux4051Channel x)
{
	// asm volatile("nop \n nop \n nop");
	gpio_put(instance->select_pin_a, (x & 0b001) >> 0);
	gpio_put(instance->select_pin_b, (x & 0b010) >> 1);
	gpio_put(instance->select_pin_c, (x & 0b100) >> 2);
	// asm volatile("nop \n nop \n nop");
}

//////////////////////////////////////
// Private
//////////////////////////////////////
