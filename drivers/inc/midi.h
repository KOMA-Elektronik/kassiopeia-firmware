#pragma once

#include "nano_midi.h"

#include <stdbool.h>
#include <stdint.h>

#define MIDI_MAX_VALUE 127
#define MIDI_MIN_VALUE 0

bool is_midi_learn_mode_active(void);
void set_midi_learn_mode(bool mode);

uint8_t get_midi_learn_note_counter(void);
uint8_t get_midi_learn_cc_counter(void);

void handle_midi_msg(struct midi_msg* msg);
void handle_midi_learn_msg(struct midi_msg* msg);