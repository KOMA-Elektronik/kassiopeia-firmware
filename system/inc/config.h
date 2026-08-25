#ifndef _KASSIOPEIA_H_
#define _KASSIOPEIA_H_

#include <stdint.h>

//////////////////////////////////////////////////////////////////////////////////
//
// DEFINES, CONFIGS & DATA STRUCTURES
//
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////
// HARDWARE CONFIG
//////////////////////////////////////

#define N_CHANNELS 4
#define ADC_MAX_VAL 4095

// HELPERS
#define LOW 0
#define HIGH 1

// ADC CONFIG
#define CAPTURE_CHANNEL 0
#define I_SENSE_ADC_CHANNEL 1
#define V_SENSE_ADC_CHANNEL 2
#define ADC_PIN 26
#define I_SENSE_PIN 27
#define V_SENSE_PIN 28
#define SAMPLE_MULTIPLIER 15

// MUX CONFIG
#define MUX_SELECT_PIN_A 10
#define MUX_SELECT_PIN_B 11
#define MUX_SELECT_PIN_C 12

// OUTPUT SELECT SWITCH
#define OUT_SEL_PIN_1 29
#define OUT_SEL_PIN_2 8
#define OUT_SEL_PIN_3 13
#define OUT_SEL_PIN_4 14

// MIDI LEARN BUTTON
#define MIDI_LEARN_PIN 17

// PWM OUTPUT
#define PWM_OUT_PIN_1 0
#define PWM_OUT_PIN_2 2
#define PWM_OUT_PIN_3 4
#define PWM_OUT_PIN_4 15

// CV_WATCH
#define CV_WATCH_PIN_1 20
#define CV_WATCH_PIN_2 19
#define CV_WATCH_PIN_3 18
#define CV_WATCH_PIN_4 16

// PULSE OUT
#define PULSE_OUT_PIN_1 1
#define PULSE_OUT_PIN_2 3
#define PULSE_OUT_PIN_3 5
#define PULSE_OUT_PIN_4 7

// LED CONFIG
#define LED1 24
#define LED2 25
#define LED3 21
#define LED4 22
#define LED_POWER 23

// PR3 and higher identifier pin
#define PR3_PLUS_PIN 6

// UART config
#define UART_ID uart1
#define BAUD_RATE 31250
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE
#define UART_BUFFER_LENGTH 3
#define MIDI_RX_PIN 9

//////////////////////////////////////
// FLASH
//////////////////////////////////////

#define FLASH_ADDRESSABLE_SIZE_BYTES                                           \
	(2 * 1024 * 1024) // Way smaller than the actual 128MB

//////////////////////////////////////
// SOFTWARE CONFIG
//////////////////////////////////////

#define MIDI_CC 0	// use MIDI CC for PWM output
#define MIDI_VELOCITY 1 // use MIDI velocity for PWM output

// choose what you want to use for PWM control
#define PWM_CONTROL_TYPE MIDI_CC

// PWM config
#define PWM_WRAP_VALUE 0xFFF

// MIDI config is stored in flash
#define MIDI_CONFIG_FLASH_OFFSET                                               \
	(FLASH_ADDRESSABLE_SIZE_BYTES -                                        \
	 FLASH_SECTOR_SIZE) // equals 0x1FF000 (2093056)
#define MIDI_LISTEN_TO_ALL -1

// MIDI LEARN
#define MIDI_LEARN_LED_ON_TIME 200	 // ms
#define MIDI_LEARN_HOLD_OFF_TIME 2000000 // us
#define MIDI_LEARN_DIM_LED_STRENGTH (UINT8_MAX >> 5)

// TRIGGER TIME
#define TRIGGER_TIME_MIN 20		 // ms
#define TRIGGER_TIME_MAX 50		 // ms
#define TRIGGER_TIME_HOLD_OFF_RISE 20000 // us
#define TRIGGER_TIME_HOLD_OFF_FALL 20000 // us

// Power monitoring
#define POWER_MON_READOUT_TIME 100 // ms
#ifndef POWER_MONITOR_DEBUG_LOG
#define POWER_MONITOR_DEBUG_LOG 0 // bench-only 10 Hz USB diagnostics
#endif
#define V_SENSE_SACLER 0.006284179688f // V / bit
#define I_SENSE_SACLER 0.5923999023f   // mA / bit

// Initial bench-test values, not production-calibrated. Voltage and current are
// measured immediately after the input bridge. Calibrate the overload budgets,
// recovery rates, and current-monitor overrange points at the highest supported
// ambient temperature.
// From a cold state, the power budget trips at about 4 s @ 21 W, 1 s @ 24 W,
// or 0.4 s @ 30 W. The bridge-current budget trips at about 5 s @ 2.31 A,
// 1 s @ 2.35 A, or 0.5 s @ 2.4 A and fully cools in 2.5 s. Sensor-overrange
// timing takes precedence once its raw threshold is reached.
#define MAX_POWER_TARGET 20.0f	     // W, continuous
#define MAX_CURRENT_TARGET 2.3f	     // A, continuous bridge current
#define POWER_OVERLOAD_BUDGET_J 4.0f // W * s above MAX_POWER_TARGET
// Repay power debt using the actual unused headroom below 20 W, limited to
// 4 J/s. Debt is clamped at zero, so idle time cannot bank future credit; the
// cap gives a full 4 J bucket a minimum one-second recovery time.
#define POWER_OVERLOAD_RECOVERY_CAP_W 4.0f
#define CURRENT_OVERLOAD_BUDGET_A_S                                            \
	0.05f			       // bridge-heat proxy, A * s above target
#define CURRENT_OVERLOAD_DECAY_A 0.02f // A * s / s while below target

// The theoretical current-monitor full scale is about 2.426 A, leaving little
// amplitude information above 2.3 A. These provisional raw thresholds treat
// near-full-scale readings as an unknown overrange condition. One 20-50 ms
// actuator event is allowed; a continuous or insufficiently cooled sequence is
// shut down. Tune the raw thresholds from the measured analog plateau.
#define CURRENT_OVERRANGE_ENTER_RAW 4052
#define CURRENT_OVERRANGE_EXIT_RAW 4018
#define CURRENT_OVERRANGE_CONTINUOUS_LIMIT_S 0.080f
#define CURRENT_OVERRANGE_BUDGET_S 0.120f
#define CURRENT_OVERRANGE_DECAY_S_PER_S 0.25f // a 50 ms event cools in 200 ms

// Round the 2.3 A baseline upward to an ADC code so nominal quantization does
// not slowly accumulate overload while operating at the allowed boundary.
#define CONTINUOUS_CURRENT_RAW_LIMIT                                           \
	((uint16_t)(((MAX_CURRENT_TARGET * 1000.0f) / I_SENSE_SACLER) +        \
		    0.999999f))
#define CONTINUOUS_CURRENT_LIMIT_A                                             \
	((CONTINUOUS_CURRENT_RAW_LIMIT * I_SENSE_SACLER) / 1000.0f)

//////////////////////////////////////
// DECLARATIONS
//////////////////////////////////////

typedef enum KASSIOPEIA_CHANNEL {
	CHAN_1 = 0,
	CHAN_2 = 1,
	CHAN_3 = 2,
	CHAN_4 = 3,
} out_channel_t;

// ADC
typedef struct ADC_HISTORY {
	uint16_t pot[N_CHANNELS];
	uint16_t cv[N_CHANNELS];

} adc_history_t;

// MIDI values scaled between 0 and UINT12T_MAX
typedef struct MIDI_HISTORY {
	uint16_t velocity[N_CHANNELS];
	uint16_t cc[N_CHANNELS];

} midi_history_t;

// MIDI learn values
typedef struct midi_notes_cc {
	int8_t note[N_CHANNELS];
	int8_t cc[N_CHANNELS];
	int8_t note_channel[N_CHANNELS];
	int8_t cc_channel[N_CHANNELS];
} midi_notes_cc_t;

typedef enum TRIGGER_STATE {
	ARMED = 0,
	TRIGGERED = 1,
} trigger_state_t;

extern const uint32_t LED_PIN[N_CHANNELS];
extern const uint32_t PULSE_OUT_PIN[N_CHANNELS];
extern const uint32_t PWM_OUT_PIN[N_CHANNELS];
extern midi_history_t MIDI_VALUES;
extern midi_notes_cc_t MIDI_LEARNED;

#endif
