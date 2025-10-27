#include "pulse.h"

#include <hardware/gpio.h>
#include <hardware/pwm.h>

void start_pulse(out_channel_t channel, uint32_t on_time)
{
	gpio_put(PULSE_OUT_PIN[channel], HIGH);
	pwm_set_gpio_level(LED_PIN[channel], UINT8_MAX);
	add_alarm_in_ms(on_time, end_of_pulse_callback, (void*)channel, false);
}

void end_pulse(out_channel_t channel)
{
	gpio_put(PULSE_OUT_PIN[channel], LOW);
	pwm_set_gpio_level(LED_PIN[channel], LOW);
}

int64_t end_of_pulse_callback(alarm_id_t id, void* user_data)
{
	end_pulse((out_channel_t)user_data);
	return 0;
}
