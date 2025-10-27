#include <inttypes.h>

#include <pico/stdlib.h>
#include <pico/time.h>

#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

#include "system/inc/config.h"
#include "system/inc/startup_sequence.h"

#include "drivers/inc/adc.h"
#include "drivers/inc/midi.h"
#include "drivers/inc/mux_4051.h"
#include "drivers/inc/pulse.h"
#include "drivers/inc/pwm.h"

#include "third-party/inc/nano_midi.h"

//////////////////////////////////////////////////////////////////////////////////
//
// GLOBAL VARIABLES
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
const midi_notes_cc_t* flash_buffer =
    (midi_notes_cc_t*)(XIP_BASE + MIDI_CONFIG_FLASH_OFFSET);
Mux4051 MUX;
struct midi_buffer MIDI_PARSER;
struct midi_buffer MIDI_LEARN_PARSER;

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

//////////////////////////////////////
// HELPERS
//////////////////////////////////////

// Save MIDI Config to Flash
void save_midi_config_to_flash(void)
{
	uint32_t interrupts = save_and_disable_interrupts();
	flash_range_erase((MIDI_CONFIG_FLASH_OFFSET), FLASH_PAGE_SIZE);
	flash_range_program((MIDI_CONFIG_FLASH_OFFSET),
			    (uint8_t*)(&MIDI_LEARNED), FLASH_PAGE_SIZE);
	restore_interrupts(interrupts);
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
	static bool power_led_state = 0;
	power_led_state ^= 1;

	// Toggle the power LED
	if (power_led_state)
		pwm_set_gpio_level(LED_POWER, UINT8_MAX);
	else
		pwm_set_gpio_level(LED_POWER, LOW);

	// If we are in MIDI learn mode, we want to keep blinking the LED
	if (is_midi_learn_mode_active())
		add_alarm_in_ms(MIDI_LEARN_LED_ON_TIME, midi_learn_callback,
				NULL, false);

	else // Save the MIDI config to flash and turn on the LED
	{
		pwm_set_gpio_level(LED_POWER, UINT8_MAX);
		save_midi_config_to_flash();
		MIDI_LEARNED = *flash_buffer;
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
		if (get_midi_learn_note_counter() <= N_CHANNELS)
			pwm_set_gpio_level(
			    LED_PIN[get_midi_learn_note_counter()],
			    MIDI_LEARN_DIM_LED_STRENGTH);
#if PWM_CONTROL_TYPE == MIDI_CC
		else
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

	MIDI_LEARNED = *flash_buffer;

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
				MIDI_LEARNED = *flash_buffer;
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
	// Enable Interrupts
	///////////////////////////////////////

	irq_set_enabled(PWM_IRQ_WRAP, true);
	irq_set_enabled(UART1_IRQ, true);

	///////////////////////////////////////
	// MAIN LOOP (LOWEST PRIORITY)
	///////////////////////////////////////

	// sampling each input @ ~2.4 kHz
	while (1) {
		// looping in gray code fashion to only change on pin at
		// the time https://en.wikipedia.org/wiki/Gray_code

		switch_to(&MUX, MUX_4051_CH_0);
		ADC_VALUES.pot[CHAN_1] = adc_read_accurate(SAMPLE_MULTIPLIER);

		switch_to(&MUX, MUX_4051_CH_1);
		ADC_VALUES.cv[CHAN_1] = adc_read_accurate(SAMPLE_MULTIPLIER);

		switch_to(&MUX, MUX_4051_CH_3);
		ADC_VALUES.pot[CHAN_2] = adc_read_accurate(SAMPLE_MULTIPLIER);

		switch_to(&MUX, MUX_4051_CH_2);
		ADC_VALUES.cv[CHAN_2] = adc_read_accurate(SAMPLE_MULTIPLIER);

		switch_to(&MUX, MUX_4051_CH_6);
		ADC_VALUES.cv[CHAN_4] = adc_read_accurate(SAMPLE_MULTIPLIER);

		switch_to(&MUX, MUX_4051_CH_7);
		ADC_VALUES.pot[CHAN_4] = adc_read_accurate(SAMPLE_MULTIPLIER);

		switch_to(&MUX, MUX_4051_CH_5);
		ADC_VALUES.pot[CHAN_3] = adc_read_accurate(SAMPLE_MULTIPLIER);

		switch_to(&MUX, MUX_4051_CH_4);
		ADC_VALUES.cv[CHAN_3] = adc_read_accurate(SAMPLE_MULTIPLIER);
	}
}
