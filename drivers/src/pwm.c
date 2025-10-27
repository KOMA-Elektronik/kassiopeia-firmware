#include "pwm.h"

#include <hardware/pwm.h>

void set_pwm(out_channel_t channel, uint16_t value)
{
	uint32_t squared = (uint32_t)(value) * (uint32_t)(value);
	pwm_set_gpio_level(PWM_OUT_PIN[channel], value);
	pwm_set_gpio_level(LED_PIN[channel], squared >> 16);
}