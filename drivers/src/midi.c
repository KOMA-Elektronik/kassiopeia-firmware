#include "midi.h"

#include <config.h>
#include <pulse.h>

bool is_midi_learn_mode = false;
uint8_t midi_learn_note_counter = 0;
uint8_t midi_learn_cc_counter = 0;

uint8_t get_midi_learn_note_counter(void) { return midi_learn_note_counter; }
uint8_t get_midi_learn_cc_counter(void) { return midi_learn_cc_counter; }

// Maps a midi value (0-127) to (0-4095)
uint16_t map_midi_to_12bit(uint8_t val) { return (val * 516) >> 4; }

// Maps a midi value (0-127) to a range defined by min and max
uint16_t map_midi_to_range(uint8_t val, uint16_t min, uint16_t max)
{
	if (val == MIDI_MIN_VALUE)
		return min;
	else if (val == MIDI_MAX_VALUE)
		return max;
	else
		return ((uint32_t)(max - min) * val) / MIDI_MAX_VALUE + min;
}

bool is_midi_learn_mode_active(void) { return is_midi_learn_mode; }

void set_midi_learn_mode(bool mode)
{
	is_midi_learn_mode = mode;

	if (is_midi_learn_mode) {
		midi_learn_cc_counter = 0;
		midi_learn_note_counter = 0;
	}
}

void handle_midi_msg(struct midi_msg* msg)
{
	switch (msg->type & 0xF0) {
	case MIDI_NOTE_ON:
		for (out_channel_t channel = CHAN_1; channel < N_CHANNELS;
		     ++channel) {
			uint8_t note_on = msg->data[0];
			uint8_t velocity = msg->data[1];

			if (note_on == MIDI_LEARNED.note[channel]) {
				if (MIDI_LEARNED.note_channel[channel] ==
					MIDI_LISTEN_TO_ALL ||
				    msg->channel ==
					MIDI_LEARNED.note_channel[channel]) {
#if PWM_CONTROL_TYPE == MIDI_VELOCITY
					MIDI_VALUES.velocity[channel] =
					    map_midi_to_12bit(
						velocity); // save velocity for
							   // PWM mode
#endif
					if (velocity > 0)
						// convert velocity to trigger
						// time in ms
						start_pulse(
						    channel,
						    map_midi_to_range(
							velocity,
							TRIGGER_TIME_MIN,
							TRIGGER_TIME_MAX));
					else // interrupt if note-on disguised
					     // as note-off comes in early
						end_pulse(channel);
					break;
				}
				// else fall through to next note
			}
		}

		break; // end of NOTE ON

	case MIDI_NOTE_OFF:
		for (out_channel_t channel = CHAN_1; channel < N_CHANNELS;
		     ++channel) {
			uint8_t note_off = msg->data[0];

			if (note_off == MIDI_LEARNED.note[channel]) {
				if (MIDI_LEARNED.note_channel[channel] ==
					MIDI_LISTEN_TO_ALL ||
				    msg->channel ==
					MIDI_LEARNED.note_channel[channel]) {
					end_pulse(channel);
					break;
				}
				// else fall through to next note
			}
		}

		break; // end of NOTE OFF

#if PWM_CONTROL_TYPE == MIDI_CC
	case MIDI_CONTROL_CHANGE:
		for (out_channel_t channel = CHAN_1; channel < N_CHANNELS;
		     ++channel) {
			uint8_t cc_value = msg->data[0];
			uint8_t cc_data = msg->data[1];

			if (cc_value == MIDI_LEARNED.cc[channel]) {
				if (MIDI_LEARNED.cc_channel[channel] ==
					MIDI_LISTEN_TO_ALL ||
				    msg->channel ==
					MIDI_LEARNED.cc_channel[channel]) {
					MIDI_VALUES.cc[channel] =
					    map_midi_to_12bit(
						cc_data); // save CC value for
							  // PWM mode
					break;
				}
				// else fall through to next CC
			}
		}

		break; // end of CONTROL CHANGE
#endif
	default:
		break;
	}
}

void handle_midi_learn_msg(struct midi_msg* msg)
{
	switch (msg->type & 0xF0) {
	case MIDI_NOTE_ON:
		switch (midi_learn_note_counter) {
		case CHAN_1:
			start_pulse(CHAN_1, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.note_channel[CHAN_1] = msg->channel;
			MIDI_LEARNED.note[CHAN_1] = msg->data[0];
			midi_learn_note_counter++;
			break;
		case CHAN_2:
			if (msg->data[0] == MIDI_LEARNED.note[CHAN_1] &&
			    msg->channel == MIDI_LEARNED.note_channel[CHAN_1])
				break; // double assignment is illegal

			start_pulse(CHAN_2, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.note_channel[CHAN_2] = msg->channel;
			MIDI_LEARNED.note[CHAN_2] = msg->data[0];
			midi_learn_note_counter++;
			break;
		case CHAN_3:
			if ((msg->data[0] == MIDI_LEARNED.note[CHAN_1] &&
			     msg->channel ==
				 MIDI_LEARNED.note_channel[CHAN_1]) ||
			    (msg->data[0] == MIDI_LEARNED.note[CHAN_2] &&
			     msg->channel == MIDI_LEARNED.note_channel[CHAN_2]))
				break; // double assignment is illegal

			start_pulse(CHAN_3, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.note_channel[CHAN_3] = msg->channel;
			MIDI_LEARNED.note[CHAN_3] = msg->data[0];
			midi_learn_note_counter++;
			break;
		case CHAN_4:
			if ((msg->data[0] == MIDI_LEARNED.note[CHAN_1] &&
			     msg->channel ==
				 MIDI_LEARNED.note_channel[CHAN_1]) ||
			    (msg->data[0] == MIDI_LEARNED.note[CHAN_2] &&
			     msg->channel ==
				 MIDI_LEARNED.note_channel[CHAN_2]) ||
			    (msg->data[0] == MIDI_LEARNED.note[CHAN_3] &&
			     msg->channel == MIDI_LEARNED.note_channel[CHAN_3]))
				break; // double assignment is illegal

			start_pulse(CHAN_4, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.note_channel[CHAN_4] = msg->channel;
			MIDI_LEARNED.note[CHAN_4] = msg->data[0];
			midi_learn_note_counter++;
#if PWM_CONTROL_TYPE == MIDI_VELOCITY
			is_midi_learn_mode =
			    false; // exit MIDI learn mode after 4 notes
#endif
			break;

		default:
			// unreachable
			break;
		}

		break; // end of MIDI LEARN NOTE

#if PWM_CONTROL_TYPE == MIDI_CC
	case MIDI_CONTROL_CHANGE:
		// make sure all CC messages are ignored while notes are being
		// configured
		switch (midi_learn_cc_counter + midi_learn_note_counter) {
		case CHAN_1 + N_CHANNELS:
			start_pulse(CHAN_1, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.cc_channel[CHAN_1] = msg->channel;
			MIDI_LEARNED.cc[CHAN_1] = msg->data[0];
			midi_learn_cc_counter++;
			break;
		case CHAN_2 + N_CHANNELS:
			if (msg->data[0] == MIDI_LEARNED.cc[CHAN_1] &&
			    msg->channel == MIDI_LEARNED.cc_channel[CHAN_1])
				break; // double assignment is illegal

			start_pulse(CHAN_2, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.cc_channel[CHAN_2] = msg->channel;
			MIDI_LEARNED.cc[CHAN_2] = msg->data[0];
			midi_learn_cc_counter++;
			break;
		case CHAN_3 + N_CHANNELS:
			if ((msg->data[0] == MIDI_LEARNED.cc[CHAN_1] &&
			     msg->channel == MIDI_LEARNED.cc_channel[CHAN_1]) ||
			    (msg->data[0] == MIDI_LEARNED.cc[CHAN_2] &&
			     msg->channel == MIDI_LEARNED.cc_channel[CHAN_2]))
				break; // double assignment is illegal

			start_pulse(CHAN_3, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.cc_channel[CHAN_3] = msg->channel;
			MIDI_LEARNED.cc[CHAN_3] = msg->data[0];
			midi_learn_cc_counter++;
			break;
		case CHAN_4 + N_CHANNELS:
			if ((msg->data[0] == MIDI_LEARNED.cc[CHAN_1] &&
			     msg->channel == MIDI_LEARNED.cc_channel[CHAN_1]) ||
			    (msg->data[0] == MIDI_LEARNED.cc[CHAN_2] &&
			     msg->channel == MIDI_LEARNED.cc_channel[CHAN_2]) ||
			    (msg->data[0] == MIDI_LEARNED.cc[CHAN_3] &&
			     msg->channel == MIDI_LEARNED.cc_channel[CHAN_3]))
				break; // double assignment is illegal

			start_pulse(CHAN_4, MIDI_LEARN_LED_ON_TIME);
			MIDI_LEARNED.cc_channel[CHAN_4] = msg->channel;
			MIDI_LEARNED.cc[CHAN_4] = msg->data[0];
			midi_learn_cc_counter++;
			is_midi_learn_mode =
			    false; // exit MIDI learn mode after 4 notes & 4 cc
				   // messages
			break;

		default:
			// unreachable
			break;
		}

		break; // end of MIDI LEARN CC
#endif

	default:
		break;
	}
}