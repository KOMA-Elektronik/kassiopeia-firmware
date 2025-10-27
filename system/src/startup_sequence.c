#include "startup_sequence.h"

#include <stdbool.h>

#include <pico/time.h>

#include "config.h"

#include <hardware/gpio.h>
#include <hardware/pwm.h>

const int led_pins[] = {LED1, LED2, LED3, LED4, LED_POWER};
const int num_leds = 5;
const uint32_t seed_max_val = 4 * 4096; // four pots @ 12 bit resolution
uint16_t current_seed = 0.0f;
float norm_seed = 0.0f;

// Use a struct to manage each LED's state
typedef struct {
	int current_pwm;
	int target_pwm;
	int wait;
	int wait_counter;
	int fade_increment;
	bool fade_direction;
	bool is_running;
} led_state_t;

void rng_recalc(void)
{
	const uint32_t a = 214013;
	const uint32_t c = 25311;

	current_seed = (a * current_seed + c) % seed_max_val;
	norm_seed = (float)current_seed / seed_max_val;
}

uint8_t rng_get_u8_between(uint8_t from, uint8_t to)
{
	rng_recalc();
	return (uint8_t)((float)(to - from) * norm_seed) + from;
}

// returns true if sequence is finished
bool star_fade(led_state_t* current)
{
	// sequence has reached end
	if ((current->current_pwm <= 0) && current->fade_direction) {
		current->current_pwm = 0; // don't wrap around
		return true;
	}

	// Check if LED is done with increase direction
	if ((current->current_pwm >= current->target_pwm) &&
	    !current->fade_direction) {

		current->fade_direction = true; // fade until 0
		current->target_pwm = 0;
	}

	// Fade the LED towards its target
	if (!current->fade_direction) {
		current->current_pwm += current->fade_increment;
	} else {
		current->current_pwm -= current->fade_increment;
	}

	return false; // still running
}

// returns true if the particular sequence is still running
void star_check(led_state_t* current)
{
	if (current->is_running) {
		if (star_fade(current)) {
			current->is_running = false;
			current->wait_counter = 0;
			current->fade_direction = false; // increase intesity

			current->target_pwm = rng_get_u8_between(100, 255);
			current->wait = rng_get_u8_between(5, 60);
			current->fade_increment = rng_get_u8_between(2, 15);
		}
	} else if (++(current->wait_counter) >= current->wait) {
		current->is_running = true; // start new sequence
	}
}

// returns true if the particular sequence is still running
bool star_fade_in(led_state_t* current)
{
	// Check if LED is done with increase direction
	if (current->current_pwm >= current->target_pwm) {
		current->current_pwm = current->target_pwm;
		current->is_running = false;
		return true; // reached end
	} else
		current->current_pwm += current->fade_increment;

	return false; // still running
}

void star_blinking_sequence(uint16_t seed)
{

	led_state_t led_states[num_leds];

	const int total_duration_ms = 2500;
	const int update_interval_ms = 16; // ~ 60 updates/second

	current_seed = seed;

	// first round
	for (int i = 0; i < num_leds; i++) {
		led_states[i].current_pwm = 0;
		led_states[i].fade_direction = false;
		led_states[i].wait_counter = 0;

		led_states[i].target_pwm = rng_get_u8_between(100, 255);
		led_states[i].wait = rng_get_u8_between(5, 60);
		led_states[i].fade_increment = rng_get_u8_between(2, 15);

		led_states[i].is_running = false;

		pwm_set_gpio_level(led_pins[i], 0);
	}

	// run sequence
	for (int t = 0; t < total_duration_ms; t += update_interval_ms) {
		for (int i = 0; i < num_leds; i++) {
			star_check(&led_states[i]);
			pwm_set_gpio_level(led_pins[i],
					   led_states[i].current_pwm);
		}
		busy_wait_ms(update_interval_ms);
	}

	// run pre-exit sequence
	while (1) {
		uint8_t exit_flags = 0;

		for (int i = 0; i < num_leds; i++) {
			if (led_states[i].is_running)
				star_check(&led_states[i]);

			pwm_set_gpio_level(led_pins[i],
					   led_states[i].current_pwm);

			exit_flags += led_states[i].is_running;
		}

		if (exit_flags == 0)
			break;

		busy_wait_ms(update_interval_ms);
	}

	led_states[4].target_pwm = 255;
	led_states[4].fade_increment = 3;

	led_states[0].target_pwm = 0;
	led_states[1].target_pwm = 0;
	led_states[2].target_pwm = 0;
	led_states[3].target_pwm = 0;

	led_states[4].is_running = true;
	led_states[0].is_running = false;
	led_states[1].is_running = false;
	led_states[2].is_running = false;
	led_states[3].is_running = false;

	// run exit sequence
	while (1) {
		uint8_t exit_flags = 0;

		for (int i = 0; i < num_leds; i++) {
			// if (led_states[i].is_running)
			if (!star_fade_in(&led_states[i])) {
				pwm_set_gpio_level(led_pins[i],
						   led_states[i].current_pwm);
			}
			exit_flags += led_states[i].is_running;
		}

		if (exit_flags == 0)
			break;

		busy_wait_ms(update_interval_ms);
	}
}