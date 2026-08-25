#include "power_monitor.h"

// Update the bridge-current thermal proxy. Above the continuous boundary it
// stores excess ampere-seconds; below the boundary it leaks at a fixed rate,
// while the exact boundary is neutral. Unlike power recovery, current recovery
// intentionally does not depend on how far below the boundary the sample lies.
static float update_accumulator(float accumulated, float measurement,
					float continuous_limit, float decay_rate,
					float elapsed_s)
{
	if (measurement > continuous_limit)
		accumulated += (measurement - continuous_limit) * elapsed_s;
	else if (measurement < continuous_limit) {
		const float decay = decay_rate * elapsed_s;
		accumulated = accumulated > decay ? accumulated - decay : 0.0f;
	}

	return accumulated;
}

// Update electrical power debt in joules. Below the continuous target, actual
// unused headroom repays debt, but never faster than recovery_cap_w. Clamping
// at zero prevents a long idle period from banking credit for a future burst.
static float update_power_accumulator(float accumulated, float power_w,
				      float continuous_limit_w,
				      float recovery_cap_w, float elapsed_s)
{
	if (power_w > continuous_limit_w) {
		accumulated += (power_w - continuous_limit_w) * elapsed_s;
	} else if (power_w < continuous_limit_w && recovery_cap_w > 0.0f) {
		const float headroom_w = continuous_limit_w - power_w;
		const float recovery_w =
			headroom_w < recovery_cap_w ? headroom_w : recovery_cap_w;
		const float recovery_j = recovery_w * elapsed_s;
		accumulated = accumulated > recovery_j
				      ? accumulated - recovery_j
				      : 0.0f;
	}

	return accumulated;
}

// Explicitly initialize every field so callers need not depend on zeroed
// storage or struct layout when re-arming the model.
void power_monitor_reset(power_monitor_state_t *state)
{
	state->power_overload_j = 0.0f;
	state->current_overload_a_s = 0.0f;
	state->current_overrange_continuous_s = 0.0f;
	state->current_overrange_exposure_s = 0.0f;
	state->trip_reason = POWER_TRIP_NONE;
}

// Advance every protection lane for one zero-order-held measurement interval,
// then latch the highest-priority reason whose configured limit was reached.
power_trip_reason_t power_monitor_update(
	power_monitor_state_t *state, const power_monitor_limits_t *limits,
	float power_w, float current_a, bool current_sensor_overrange,
	float elapsed_s)
{
	// A safety trip is a latch: cooling samples cannot automatically restore
	// outputs after a fault.
	if (state->trip_reason != POWER_TRIP_NONE)
		return state->trip_reason;

	// Protect the accumulators from timer initialization and clock anomalies.
	if (elapsed_s <= 0.0f)
		return POWER_TRIP_NONE;

	// The current and power envelopes protect different hardware constraints,
	// so both accumulate from every valid measurement interval.
	state->current_overload_a_s = update_accumulator(
		state->current_overload_a_s, current_a,
		limits->continuous_current_a, limits->current_overload_decay_a,
		elapsed_s);
	state->power_overload_j = update_power_accumulator(
		state->power_overload_j, power_w, limits->continuous_power_w,
		limits->power_overload_recovery_cap_w, elapsed_s);

	// Above the current monitor's calibrated range the sample is only a lower
	// bound. Allow short actuator pulses, but limit both one continuous event
	// and repeated events with insufficient cooling time.
	if (current_sensor_overrange) {
		state->current_overrange_continuous_s += elapsed_s;
		state->current_overrange_exposure_s += elapsed_s;
	} else {
		const float decay =
			limits->current_overrange_decay_s_per_s * elapsed_s;
		state->current_overrange_continuous_s = 0.0f;
		state->current_overrange_exposure_s =
			state->current_overrange_exposure_s > decay
				? state->current_overrange_exposure_s - decay
				: 0.0f;
	}

	// Evaluate unknown-amplitude overrange first, then the measurable bridge
	// current proxy, then electrical power. The first reason reached is retained.
	// Excess current-time is proportional to excess bridge heat:
	// E_bridge ~= 2 * V_f * current_overload_a_s.
	if (state->current_overrange_continuous_s >=
		    limits->current_overrange_continuous_limit_s ||
	    state->current_overrange_exposure_s >=
		    limits->current_overrange_budget_s)
		state->trip_reason = POWER_TRIP_CURRENT_SENSOR_OVERRANGE;
	else if (state->current_overload_a_s >=
	    limits->current_overload_budget_a_s)
		state->trip_reason = POWER_TRIP_CURRENT_OVERLOAD;
	else if (state->power_overload_j >= limits->power_overload_budget_j)
		state->trip_reason = POWER_TRIP_POWER_OVERLOAD;

	return state->trip_reason;
}
