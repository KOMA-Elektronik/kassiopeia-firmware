
#ifndef _DRIVRES_PULSE_H_
#define _DRIVRES_PULSE_H_

#include <config.h>
#include <pico/time.h>
#include <stdint.h>

void start_pulse(out_channel_t channel, uint32_t on_time);
void end_pulse(out_channel_t channel);

int64_t end_of_pulse_callback(alarm_id_t id, void* user_data);

#endif