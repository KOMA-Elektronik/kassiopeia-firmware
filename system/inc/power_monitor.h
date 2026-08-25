#pragma once

#include <stdbool.h>

/**
 * Pure, hardware-independent protection model for the post-bridge power bus.
 *
 * The caller supplies engineering-unit measurements and their real elapsed
 * time. The monitor maintains three independent protection lanes: excess
 * electrical power, excess bridge current, and current-sensor overrange. It
 * reports a latched trip reason but performs no GPIO, logging, timing, or ADC
 * work itself. Measurements, thresholds, and recovery rates are expected to be
 * finite and non-negative.
 */

typedef enum POWER_TRIP_REASON {
	POWER_TRIP_NONE = 0, // Monitor is armed and no limit has been reached.
	// Current was unmeasurably high for too long or too frequently.
	POWER_TRIP_CURRENT_SENSOR_OVERRANGE,
	// The measurable excess-current budget was exhausted.
	POWER_TRIP_CURRENT_OVERLOAD,
	// The excess-power energy budget was exhausted.
	POWER_TRIP_POWER_OVERLOAD,
} power_trip_reason_t;

/** Tunable limits; suffixes state the required physical units. */
typedef struct POWER_MONITOR_LIMITS {
	float continuous_power_w; // Power at or below this creates no debt.
	float power_overload_budget_j; // Trip threshold for excess-power energy.
	// Maximum rate at which below-target power may repay power debt.
	float power_overload_recovery_cap_w;
	float continuous_current_a; // Current at or below this creates no debt.
	float current_overload_budget_a_s; // Excess-current trip threshold.
	float current_overload_decay_a; // Fixed A*s repaid per elapsed second.
	// Maximum duration of one uninterrupted sensor-overrange event.
	float current_overrange_continuous_limit_s;
	// Leaky time budget shared by repeated overrange events.
	float current_overrange_budget_s;
	// Overrange exposure seconds repaid per non-overrange second.
	float current_overrange_decay_s_per_s;
} power_monitor_limits_t;

/** Runtime state. Initialize with power_monitor_reset(), not field defaults. */
typedef struct POWER_MONITOR_STATE {
	float power_overload_j; // Excess-power energy currently retained.
	// For a fixed diode V_f, excess bridge energy is approximately
	// 2 * V_f * current_overload_a_s.
	float current_overload_a_s;
	// Length of the current uninterrupted overrange event; resets below range.
	float current_overrange_continuous_s;
	// Leaky exposure retained across separate overrange events.
	float current_overrange_exposure_s;
	power_trip_reason_t trip_reason; // First trip latches until reset.
} power_monitor_state_t;

/**
 * Restore a cold, armed state with all accumulated protection debt cleared.
 *
 * @param state Monitor state to initialize or explicitly re-arm.
 */
void power_monitor_reset(power_monitor_state_t *state);

/**
 * Advance the protection model by one elapsed interval.
 *
 * power_w and current_a are assumed to have been constant for elapsed_s. The
 * integration code should therefore pass the measurement that covered the
 * interval, not a new endpoint measurement. A non-positive elapsed_s is a
 * no-op. Once a trip occurs, subsequent calls return the same reason without
 * modifying any accumulator; only power_monitor_reset() re-arms the monitor.
 *
 * @param state Mutable accumulator and latch state.
 * @param limits Protection thresholds and recovery rates.
 * @param power_w Post-bridge bus power in watts.
 * @param current_a Series bridge/load current in amperes.
 * @param current_sensor_overrange True when current amplitude is only a lower
 *        bound because the analog current monitor is near saturation.
 * @param elapsed_s Time represented by this measurement, in seconds.
 * @return POWER_TRIP_NONE while armed, otherwise the latched trip reason.
 */
power_trip_reason_t power_monitor_update(
	power_monitor_state_t *state, const power_monitor_limits_t *limits,
	float power_w, float current_a, bool current_sensor_overrange,
	float elapsed_s);
