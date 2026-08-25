#include <inttypes.h>

#include <stdio.h>

#include <pico/stdlib.h>
#include <pico/time.h>

#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

#include "system/inc/config.h"
#include "system/inc/power_monitor.h"
#include "system/inc/startup_sequence.h"

#include "drivers/inc/adc.h"
#include "drivers/inc/midi.h"
#include "drivers/inc/mux_4051.h"
#include "drivers/inc/pulse.h"
#include "drivers/inc/pwm.h"

#include "third-party/inc/nano_midi.h"

//////////////////////////////////////////////////////////////////////////////////
//
// GLOBAL VARIABLES STORED IN RAM
//
//////////////////////////////////////////////////////////////////////////////////

adc_history_t ADC_VALUES = {0};
midi_history_t MIDI_VALUES = {0};
midi_notes_cc_t MIDI_LEARNED = {
    .note = {0, 2, 4, 5},   // factory notes
    .cc = {20, 21, 22, 23}, // factory cc numbers
    .note_channel = {MIDI_LISTEN_TO_ALL, MIDI_LISTEN_TO_ALL, MIDI_LISTEN_TO_ALL,
		     MIDI_LISTEN_TO_ALL},
    .cc_channel = {MIDI_LISTEN_TO_ALL, MIDI_LISTEN_TO_ALL, MIDI_LISTEN_TO_ALL,
		   MIDI_LISTEN_TO_ALL}};
Mux4051 MUX;
struct midi_buffer MIDI_PARSER;
struct midi_buffer MIDI_LEARN_PARSER;

//////////////////////////////////////////////
// Power monitoring
bool pr3_or_higher = false;
volatile bool power_shut_down = false;
float max_power_target = MAX_POWER_TARGET;
power_monitor_state_t POWER_MONITOR = {0};
//////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
//
// GLOBAL VARIABLES STORED IN FLASH
//
//////////////////////////////////////////////////////////////////////////////////

const midi_notes_cc_t* midi_config_flash =
    (midi_notes_cc_t*)(XIP_BASE + MIDI_CONFIG_FLASH_OFFSET);

//////////////////////////////////////////////////////////////////////////////////
//
// CONSTS
//
//////////////////////////////////////////////////////////////////////////////////

const uint32_t PWM_OUT_PIN[N_CHANNELS] = {PWM_OUT_PIN_1, PWM_OUT_PIN_2,
					  PWM_OUT_PIN_3, PWM_OUT_PIN_4};

const uint32_t PULSE_OUT_PIN[N_CHANNELS] = {PULSE_OUT_PIN_1, PULSE_OUT_PIN_2,
					    PULSE_OUT_PIN_3, PULSE_OUT_PIN_4};

const uint32_t OUT_SEL_PIN[N_CHANNELS] = {OUT_SEL_PIN_1, OUT_SEL_PIN_2,
					  OUT_SEL_PIN_3, OUT_SEL_PIN_4};

const uint32_t CV_WATCH_PIN[N_CHANNELS] = {CV_WATCH_PIN_1, CV_WATCH_PIN_2,
					   CV_WATCH_PIN_3, CV_WATCH_PIN_4};

const uint32_t LED_PIN[N_CHANNELS] = {LED1, LED2, LED3, LED4};

//////////////////////////////////////////////
// for PR3 and higher

// Bind the compile-time policy in config.h to the hardware-independent monitor.
const power_monitor_limits_t POWER_MONITOR_LIMITS = {
    .continuous_power_w = MAX_POWER_TARGET,
    .power_overload_budget_j = POWER_OVERLOAD_BUDGET_J,
    .power_overload_recovery_cap_w = POWER_OVERLOAD_RECOVERY_CAP_W,
    .continuous_current_a = CONTINUOUS_CURRENT_LIMIT_A,
    .current_overload_budget_a_s = CURRENT_OVERLOAD_BUDGET_A_S,
    .current_overload_decay_a = CURRENT_OVERLOAD_DECAY_A,
    .current_overrange_continuous_limit_s =
	CURRENT_OVERRANGE_CONTINUOUS_LIMIT_S,
    .current_overrange_budget_s = CURRENT_OVERRANGE_BUDGET_S,
    .current_overrange_decay_s_per_s = CURRENT_OVERRANGE_DECAY_S_PER_S,
};

//////////////////////////////////////////////

//////////////////////////////////////
// HELPERS
//////////////////////////////////////

// Immediately de-energize both output types. This is deliberately stronger
// than clearing ADC control values because MIDI state could otherwise restore
// a PWM level and an outstanding trigger could leave a pulse pin high.
static void shut_down_outputs(void)
{
	for (out_channel_t ch = CHAN_1; ch < N_CHANNELS; ++ch) {
		set_pwm(ch, 0);
		end_pulse(ch);
	}
}

// Save MIDI Config to Flash
void save_midi_config_to_flash(void)
{
	uint32_t interrupts = save_and_disable_interrupts();
	// Disable output-producing IRQs before forcing the pins low; otherwise
	// one could reassert an output between shut_down_outputs() and the IRQ
	// mask.
	shut_down_outputs();
	flash_range_erase((MIDI_CONFIG_FLASH_OFFSET), sizeof(midi_notes_cc_t));
	flash_range_program((MIDI_CONFIG_FLASH_OFFSET),
			    (uint8_t*)(&MIDI_LEARNED), sizeof(midi_notes_cc_t));
	restore_interrupts(interrupts);
}

void toggle_power_led(void)
{
	static bool power_led_state = 0;
	power_led_state ^= 1;

	// Toggle the power LED
	if (power_led_state)
		pwm_set_gpio_level(LED_POWER, UINT8_MAX);
	else
		pwm_set_gpio_level(LED_POWER, LOW);
}

#if POWER_MONITOR_DEBUG_LOG

// Return the stable diagnostic text printed on the first trip transition.
static const char* power_trip_reason_string(power_trip_reason_t reason)
{
	switch (reason) {
	case POWER_TRIP_CURRENT_SENSOR_OVERRANGE:
		return "sustained/repeated current-sensor overrange";
	case POWER_TRIP_CURRENT_OVERLOAD:
		return "accumulated bridge/current overload";
	case POWER_TRIP_POWER_OVERLOAD:
		return "accumulated power overload";
	case POWER_TRIP_NONE:
	default:
		return "none";
	}
}

#endif

// Atomically latch shutdown against the PWM interrupt and force every output
// low. Return true only to the caller that performed the first transition so
// the trip snapshot is printed once rather than on every later sample.
static bool latch_power_trip(power_trip_reason_t reason)
{
	const uint32_t interrupts = save_and_disable_interrupts();
	const bool newly_tripped = !power_shut_down;

	if (newly_tripped) {
		POWER_MONITOR.trip_reason = reason;
		power_shut_down = true;
		shut_down_outputs();
	}

	restore_interrupts(interrupts);
	return newly_tripped;
}

// Convert one sequential post-bridge ADC pair, advance the protection model
// using real elapsed time, and handle trip/logging side effects. Protection
// runs on every call; POWER_MON_READOUT_TIME controls only LED/USB reporting.
static void process_power_sample(uint16_t current_raw, uint16_t voltage_raw)
{
	static uint64_t previous_sample_us = 0;
	static uint64_t next_readout_us = 0;
	static float previous_power_w = 0.0f;
	static float previous_current_a = 0.0f;
	static uint16_t previous_current_raw = 0;
	static uint16_t previous_voltage_raw = 0;
	static bool current_sensor_overrange = false;
	static bool previous_current_sensor_overrange = false;

#if POWER_MONITOR_DEBUG_LOG

	static uint16_t peak_current_raw = 0;
	static bool interval_overrange_seen = false;
	static float interval_energy_j = 0.0f;
	static float interval_elapsed_s = 0.0f;
	static float interval_peak_power_w = 0.0f;
	static float interval_excess_j = 0.0f;
	static float interval_available_recovery_j = 0.0f;

#endif

	const uint64_t sample_time_us = time_us_64();
	const float current_a = (current_raw * I_SENSE_SACLER) / 1000.0f;
	const float voltage_v = voltage_raw * V_SENSE_SACLER;
	const float power_w = voltage_v * current_a;

	// Hysteresis prevents ADC noise near the current monitor's upper range
	// from rapidly entering and leaving the unknown-amplitude overrange
	// state.
	if (current_raw >= CURRENT_OVERRANGE_ENTER_RAW)
		current_sensor_overrange = true;
	else if (current_raw <= CURRENT_OVERRANGE_EXIT_RAW)
		current_sensor_overrange = false;

#if POWER_MONITOR_DEBUG_LOG

	// These are reporting-window statistics only. The protection model
	// below continues to update at the much faster ADC sampling cadence.
	if (current_sensor_overrange)
		interval_overrange_seen = true;

	if (current_raw > peak_current_raw)
		peak_current_raw = current_raw;

#endif

	// Integrate the previous measurement over the time for which it was the
	// latest sample. This captures 20-50 ms pulses without 100 ms aliasing.
	if (previous_sample_us != 0) {
		const float elapsed_s =
		    (sample_time_us - previous_sample_us) / 1000000.0f;

#if POWER_MONITOR_DEBUG_LOG

		interval_energy_j += previous_power_w * elapsed_s;
		interval_elapsed_s += elapsed_s;
		if (previous_power_w > interval_peak_power_w)
			interval_peak_power_w = previous_power_w;
		if (previous_power_w > MAX_POWER_TARGET) {
			interval_excess_j +=
			    (previous_power_w - MAX_POWER_TARGET) * elapsed_s;
		} else if (previous_power_w < MAX_POWER_TARGET) {
			const float headroom_w =
			    MAX_POWER_TARGET - previous_power_w;
			const float recovery_w =
			    headroom_w < POWER_OVERLOAD_RECOVERY_CAP_W
				? headroom_w
				: POWER_OVERLOAD_RECOVERY_CAP_W;
			interval_available_recovery_j += recovery_w * elapsed_s;
		}

#endif
		const power_trip_reason_t trip_reason = power_monitor_update(
		    &POWER_MONITOR, &POWER_MONITOR_LIMITS, previous_power_w,
		    previous_current_a, previous_current_sensor_overrange,
		    elapsed_s);

		if (trip_reason != POWER_TRIP_NONE &&
		    latch_power_trip(trip_reason)) {

#if POWER_MONITOR_DEBUG_LOG

			printf("Power protection tripped: %s "
			       "(raw I=%u, raw V=%u, %.3fV @ %.3fmA -> %.3fW, "
			       "%.3fJ, %.3fA*s, clip %.1f/%.1fms)\n",
			       power_trip_reason_string(trip_reason),
			       (unsigned)previous_current_raw,
			       (unsigned)previous_voltage_raw,
			       previous_voltage_raw * V_SENSE_SACLER,
			       previous_current_a * 1000.0f, previous_power_w,
			       POWER_MONITOR.power_overload_j,
			       POWER_MONITOR.current_overload_a_s,
			       POWER_MONITOR.current_overrange_continuous_s *
				   1000.0f,
			       POWER_MONITOR.current_overrange_exposure_s *
				   1000.0f);
#endif
		}
	}

	// Preserve this endpoint as the zero-order-held measurement for the
	// next elapsed interval. This avoids assigning time retroactively to a
	// new sample.
	previous_sample_us = sample_time_us;
	previous_power_w = power_w;
	previous_current_a = current_a;
	previous_current_raw = current_raw;
	previous_voltage_raw = voltage_raw;
	previous_current_sensor_overrange = current_sensor_overrange;

	// Human-readable diagnostics and shutdown blinking are intentionally
	// decoupled from protection timing.
	if (next_readout_us == 0) {
		next_readout_us =
		    sample_time_us + (POWER_MON_READOUT_TIME * 1000);
	} else if (sample_time_us >= next_readout_us) {
		next_readout_us =
		    sample_time_us + (POWER_MON_READOUT_TIME * 1000);

		if (power_shut_down)
			toggle_power_led();

#if POWER_MONITOR_DEBUG_LOG

		const float interval_average_power_w =
		    interval_elapsed_s > 0.0f
			? interval_energy_j / interval_elapsed_s
			: power_w;
		printf("%.3fV @ %.3f mA -> %.3fW; avg/max %.3f/%.3fW; "
		       "peak raw %u/%.3f mA%s "
		       "(P %.3fJ, excess/avail %.3f/%.3fJ, %.3fA*s, "
		       "clip %.1f/%.1fms)\n",
		       voltage_v, current_a * 1000.0f, power_w,
		       interval_average_power_w, interval_peak_power_w,
		       (unsigned)peak_current_raw,
		       (peak_current_raw * I_SENSE_SACLER),
		       interval_overrange_seen ? " OVERRANGE" : "",
		       POWER_MONITOR.power_overload_j, interval_excess_j,
		       interval_available_recovery_j,
		       POWER_MONITOR.current_overload_a_s,
		       POWER_MONITOR.current_overrange_continuous_s * 1000.0f,
		       POWER_MONITOR.current_overrange_exposure_s * 1000.0f);

		peak_current_raw = 0;
		interval_overrange_seen = false;
		interval_energy_j = 0.0f;
		interval_elapsed_s = 0.0f;
		interval_peak_power_w = 0.0f;
		interval_excess_j = 0.0f;
		interval_available_recovery_j = 0.0f;
#endif
	}
}

//////////////////////////////////////////////////////////////////////////////////
//
// INTERRUPT HANDLERS
//
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////
// MIDI LEARN CALLBACK
//////////////////////////////////////

int64_t midi_learn_callback(alarm_id_t id, void* user_data)
{
	if (power_shut_down)
		return 0;

	// If we are in MIDI learn mode, we want to keep blinking the LED
	if (is_midi_learn_mode_active()) {
		toggle_power_led();
		add_alarm_in_ms(MIDI_LEARN_LED_ON_TIME, midi_learn_callback,
				NULL, false);
	}
	// Save the MIDI config to flash and turn on the LED
	else {
		pwm_set_gpio_level(LED_POWER, UINT8_MAX);
		save_midi_config_to_flash();
		MIDI_LEARNED = *midi_config_flash;
	}

	return 0;
}

//////////////////////////////////////
// PWM
//////////////////////////////////////

static trigger_state_t cv_trigger[N_CHANNELS] = {0};
static uint64_t time_stamp[N_CHANNELS] = {0};

void handle_pwm(out_channel_t ch)
{
	set_pwm(ch, ADC_VALUES.pot[ch] + ADC_VALUES.cv[ch] +
#if PWM_CONTROL_TYPE == MIDI_VELOCITY
			MIDI_VALUES.velocity[ch]
#elif PWM_CONTROL_TYPE == MIDI_CC
			MIDI_VALUES.cc[ch]
#else
#error "Unsupported PWM_CONTROL_TYPE"
#endif
	);
}

// Maps 12-bit value to the range [TRIGGER_TIME_MIN, TRIGGER_TIME_MAX]
uint16_t map_time(uint16_t val)
{
	return (((uint64_t)(TRIGGER_TIME_MAX - TRIGGER_TIME_MIN) * val) /
		ADC_MAX_VAL) +
	       TRIGGER_TIME_MIN;
}

void handle_trigger_on(out_channel_t ch)
{
	// check hold off time && pin state
	if (gpio_get(CV_WATCH_PIN[ch]) && cv_trigger[ch] == ARMED &&
	    time_us_64() - time_stamp[ch] > TRIGGER_TIME_HOLD_OFF_RISE) {

		cv_trigger[ch] = TRIGGERED;
		time_stamp[ch] = time_us_64();
		start_pulse(ch, map_time(ADC_VALUES.pot[ch]));
	}
}

void handle_trigger_off(out_channel_t ch)
{
	// check hold off time && pin state
	if (!gpio_get(CV_WATCH_PIN[ch]) && cv_trigger[ch] == TRIGGERED &&
	    (time_us_64() - time_stamp[ch] > TRIGGER_TIME_HOLD_OFF_FALL)) {

		cv_trigger[ch] = ARMED;
		time_stamp[ch] = time_us_64();
		end_pulse(ch);
	}
}

// executed in irq handler
void on_pwm_wrap(void)
{
	// The latch is the final output gate: stale MIDI/CV state cannot
	// re-energize an output after shut_down_outputs() has forced the pins
	// low.
	if (power_shut_down) {
		pwm_clear_irq(pwm_gpio_to_slice_num(LED1));
		return;
	}

	if (!is_midi_learn_mode_active()) {
		static bool switched_to_trigger[N_CHANNELS] = {0};

		for (out_channel_t ch = CHAN_1; ch < N_CHANNELS; ++ch) {

			// check if in pwm or trigger mode
			if (!gpio_get(OUT_SEL_PIN[ch]) &&
			    !switched_to_trigger[ch]) {
				switched_to_trigger[ch] = true;
				pwm_set_gpio_level(LED_PIN[ch], LOW);
			}

			if (gpio_get(OUT_SEL_PIN[ch])) {
				switched_to_trigger[ch] = false;
				handle_pwm(ch);
			}

			if (switched_to_trigger[ch]) {
				handle_trigger_on(ch);
				handle_trigger_off(ch);
			}
		}
	} else {
		// dimmly light up current LED that needs to assigned
		if (get_midi_learn_note_counter() < N_CHANNELS)
			pwm_set_gpio_level(
			    LED_PIN[get_midi_learn_note_counter()],
			    MIDI_LEARN_DIM_LED_STRENGTH);
#if PWM_CONTROL_TYPE == MIDI_CC
		else if (get_midi_learn_cc_counter() +
			     get_midi_learn_note_counter() >=
			 N_CHANNELS)
			pwm_set_gpio_level(LED_PIN[get_midi_learn_cc_counter()],
					   MIDI_LEARN_DIM_LED_STRENGTH);
#endif
	}

	// Clear the interrupt flag that brought us here
	pwm_clear_irq(pwm_gpio_to_slice_num(LED1));
}

//////////////////////////////////////
// MIDI
//////////////////////////////////////

void on_uart_rx()
{
	uint8_t incoming[1];
	while (uart_is_readable(UART_ID)) {
		incoming[0] = (uint8_t)uart_getc(UART_ID);
		if (power_shut_down)
			continue;

		if (is_midi_learn_mode_active())
			midi_parse(&MIDI_LEARN_PARSER, incoming, 1);
		else
			midi_parse(&MIDI_PARSER, incoming, 1);
	}
}

//////////////////////////////////////////////////////////////////////////////////
//
// INITIALIZATION
//
//////////////////////////////////////////////////////////////////////////////////

int main()
{
	///////////////////////////////////////
	// Setup UART/MIDI
	///////////////////////////////////////

	uart_init(UART_ID, 2400);
	gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);

	// Actually, we want a different speed
	// The call will return the actual baud rate selected, which
	// will be as close as possible to that requested
	uart_set_baudrate(UART_ID, BAUD_RATE);

	// Set UART flow control CTS/RTS, we don't want these, so turn
	// them off
	uart_set_hw_flow(UART_ID, false, false);

	// Set our data format
	uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);

	// Turn off FIFO's - we want to do this message by message
	uart_set_fifo_enabled(UART_ID, true);

	// Set interrupts handler
	irq_set_exclusive_handler(UART1_IRQ, on_uart_rx);

	// Now enable the UART to send interrupts - RX only
	uart_set_irq_enables(UART_ID, true, false);

	///////////////////////////////////////
	// Setup MIDI
	///////////////////////////////////////

	midi_buffer_init(&MIDI_PARSER);
	MIDI_PARSER.callback = handle_midi_msg; // Set callback function
	MIDI_PARSER.channel_mask = 0xFFFF;	// Listen to all channels

	midi_buffer_init(&MIDI_LEARN_PARSER);
	MIDI_LEARN_PARSER.callback =
	    handle_midi_learn_msg;		 // Set callback function
	MIDI_LEARN_PARSER.channel_mask = 0xFFFF; // Listen to all channels

	///////////////////////////////////////
	// Setup ADC
	///////////////////////////////////////

	adc_gpio_init(ADC_PIN + CAPTURE_CHANNEL);
	adc_gpio_init(I_SENSE_PIN);
	adc_gpio_init(V_SENSE_PIN);

	adc_init();
	adc_select_input(CAPTURE_CHANNEL);

	// Divisor of 0 -> full speed. Free-running capture with the
	// divider is equivalent to pressing the ADC_CS_START_ONCE
	// button once per `div + 1` cycles (div not necessarily an
	// integer). Each conversion takes 96 cycles, so in general you
	// want a divider of 0 (hold down the button continuously) or >
	// 95 (take samples less frequently than 96 cycle intervals).
	// This is all timed by the 48 MHz ADC clock.
	adc_set_clkdiv(0);

	///////////////////////////////////////
	// Setup GPIOs
	///////////////////////////////////////

	gpio_set_function(MIDI_LEARN_PIN, GPIO_FUNC_SIO);
	gpio_pull_up(MIDI_LEARN_PIN);
	gpio_set_dir(MIDI_LEARN_PIN, GPIO_IN);

	for (size_t ch = 0; ch < N_CHANNELS; ++ch) {
		gpio_set_function(PULSE_OUT_PIN[ch], GPIO_FUNC_SIO);
		gpio_set_function(OUT_SEL_PIN[ch], GPIO_FUNC_SIO);
		gpio_set_function(CV_WATCH_PIN[ch], GPIO_FUNC_SIO);

		gpio_set_dir(PULSE_OUT_PIN[ch], GPIO_OUT);
		gpio_set_dir(OUT_SEL_PIN[ch], GPIO_IN);
		gpio_set_dir(CV_WATCH_PIN[ch], GPIO_IN);
	}

	gpio_set_function(PR3_PLUS_PIN, GPIO_FUNC_SIO);
	gpio_set_dir(PR3_PLUS_PIN, GPIO_IN);

	if (gpio_get(PR3_PLUS_PIN)) {
		// PR3 and higher
		pr3_or_higher = true;
#if POWER_MONITOR_DEBUG_LOG
		stdio_init_all();
#endif
	} else {
		// PR1 or PR2 -> no power monitoring
		pr3_or_higher = false;
	}

	///////////////////////////////////////
	// Setup MUX
	///////////////////////////////////////

	MUX =
	    Mux4051_init(MUX_SELECT_PIN_A, MUX_SELECT_PIN_B, MUX_SELECT_PIN_C);

	switch_to(&MUX, MUX_4051_CH_0);

	///////////////////////////////////////
	// Setup PWM
	///////////////////////////////////////

	gpio_set_function(LED_POWER, GPIO_FUNC_PWM);

	for (size_t ch = 0; ch < N_CHANNELS; ++ch) {
		gpio_set_function(LED_PIN[ch], GPIO_FUNC_PWM);
		gpio_set_function(PWM_OUT_PIN[ch], GPIO_FUNC_PWM);
	}

	// Figure out which slice we just connected to the LED pins
	uint slice_power_led = pwm_gpio_to_slice_num(LED_POWER);
	uint slice_led1_2 = pwm_gpio_to_slice_num(LED1);
	uint slice_led3 = pwm_gpio_to_slice_num(LED3);
	uint slice_led4 = pwm_gpio_to_slice_num(LED4);
	uint slice_pwm_out1 = pwm_gpio_to_slice_num(PWM_OUT_PIN_1);
	uint slice_pwm_out2 = pwm_gpio_to_slice_num(PWM_OUT_PIN_2);
	uint slice_pwm_out3 = pwm_gpio_to_slice_num(PWM_OUT_PIN_3);
	uint slice_pwm_out4 = pwm_gpio_to_slice_num(PWM_OUT_PIN_4);

	// Mask our slice's IRQ output into the PWM block's single
	// interrupt line, and register our interrupt handler
	pwm_clear_irq(slice_led1_2);
	pwm_set_irq_enabled(slice_led1_2, true);
	irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);

	// PWM wraps around at ~30kHz
	pwm_config config = pwm_get_default_config();
	pwm_config_set_wrap(&config, PWM_WRAP_VALUE);

	// Load the configuration into our PWM slices, and set them
	// running.
	pwm_init(slice_power_led, &config, true);
	pwm_init(slice_led1_2, &config, true);
	pwm_init(slice_led3, &config, true);
	pwm_init(slice_led4, &config, true);
	pwm_init(slice_pwm_out1, &config, true);
	pwm_init(slice_pwm_out2, &config, true);
	pwm_init(slice_pwm_out3, &config, true);
	pwm_init(slice_pwm_out4, &config, true);

	///////////////////////////////////////
	// Get MIDI Config from Flash
	///////////////////////////////////////

	MIDI_LEARNED = *midi_config_flash;

	if (!gpio_get(MIDI_LEARN_PIN)) {
		set_midi_learn_mode(true);

		// enable interrupts shortly
		irq_set_enabled(PWM_IRQ_WRAP, true);
		irq_set_enabled(UART1_IRQ, true);

		///////////////////////////////////////
		// MIDI LEARN ROUTINE
		///////////////////////////////////////

		// make the power led blink every 200ms
		add_alarm_in_ms(MIDI_LEARN_LED_ON_TIME, midi_learn_callback,
				NULL, false);

		uint32_t entered = time_us_32();

		while (is_midi_learn_mode_active()) {
			// latch button state until all messages are received
			if (!gpio_get(MIDI_LEARN_PIN) &&
			    (time_us_32() - entered >
			     MIDI_LEARN_HOLD_OFF_TIME)) {
				set_midi_learn_mode(false);
				save_midi_config_to_flash();
				MIDI_LEARNED = *midi_config_flash;
			}
		}

		irq_set_enabled(PWM_IRQ_WRAP, false);
		irq_set_enabled(UART1_IRQ, false);
	}

	///////////////////////////////////////
	// STARTUP SEQUENCE
	///////////////////////////////////////

	// entropy source is pot positions
	uint16_t r_seed = 0;
	adc_select_input(CAPTURE_CHANNEL);

	switch_to(&MUX, MUX_4051_CH_0);
	r_seed += adc_read_accurate(SAMPLE_MULTIPLIER);

	switch_to(&MUX, MUX_4051_CH_3);
	r_seed += adc_read_accurate(SAMPLE_MULTIPLIER);

	switch_to(&MUX, MUX_4051_CH_7);
	r_seed += adc_read_accurate(SAMPLE_MULTIPLIER);

	switch_to(&MUX, MUX_4051_CH_5);
	r_seed += adc_read_accurate(SAMPLE_MULTIPLIER);

	star_blinking_sequence(r_seed);

	///////////////////////////////////////
	// POWER MONITORING
	///////////////////////////////////////

	if (pr3_or_higher)
		power_monitor_reset(&POWER_MONITOR);

	///////////////////////////////////////
	// Enable Interrupts
	///////////////////////////////////////

	irq_set_enabled(PWM_IRQ_WRAP, true);
	irq_set_enabled(UART1_IRQ, true);

	///////////////////////////////////////
	// MAIN LOOP (LOWEST PRIORITY)
	///////////////////////////////////////

	// Continuously sample the controls and power sensors.
	while (1) {
		// looping in gray code fashion to only change on pin at
		// the time https://en.wikipedia.org/wiki/Gray_code

		adc_select_input(CAPTURE_CHANNEL);

		if (!power_shut_down) {
			switch_to(&MUX, MUX_4051_CH_0);
			ADC_VALUES.pot[CHAN_1] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);

			switch_to(&MUX, MUX_4051_CH_1);
			ADC_VALUES.cv[CHAN_1] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);

			switch_to(&MUX, MUX_4051_CH_3);
			ADC_VALUES.pot[CHAN_2] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);

			switch_to(&MUX, MUX_4051_CH_2);
			ADC_VALUES.cv[CHAN_2] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);

			switch_to(&MUX, MUX_4051_CH_6);
			ADC_VALUES.cv[CHAN_4] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);

			switch_to(&MUX, MUX_4051_CH_7);
			ADC_VALUES.pot[CHAN_4] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);

			switch_to(&MUX, MUX_4051_CH_5);
			ADC_VALUES.pot[CHAN_3] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);

			switch_to(&MUX, MUX_4051_CH_4);
			ADC_VALUES.cv[CHAN_3] =
			    adc_read_accurate(SAMPLE_MULTIPLIER);
		} else {
			// shut down triggered -> disable all outputs

			ADC_VALUES.pot[CHAN_1] = 0;
			ADC_VALUES.cv[CHAN_1] = 0;
			ADC_VALUES.pot[CHAN_2] = 0;
			ADC_VALUES.cv[CHAN_2] = 0;
			ADC_VALUES.cv[CHAN_4] = 0;
			ADC_VALUES.pot[CHAN_4] = 0;
			ADC_VALUES.pot[CHAN_3] = 0;
			ADC_VALUES.cv[CHAN_3] = 0;
		}

		if (pr3_or_higher) {
			adc_select_input(I_SENSE_ADC_CHANNEL);

			const uint16_t current_sample = adc_read_accurate(255);

			adc_select_input(V_SENSE_ADC_CHANNEL);

			const uint16_t voltage_sample = adc_read_accurate(255);

			process_power_sample(current_sample, voltage_sample);
		}
	}
}
