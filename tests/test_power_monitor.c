#include "config.h"
#include "power_monitor.h"

#include <stdbool.h>
#include <stdio.h>

#define SAMPLE_TIME_S 0.1f

// Minimal host-side assertion helper: every test returns false at its first
// failed contract and reports the source expression without a test framework.
#define CHECK(condition)                                                       \
	do {                                                                     \
		if (!(condition)) {                                                \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,     \
				__LINE__, #condition);                                \
			return false;                                              \
		}                                                                \
	} while (0)

// Exercise the same policy constants used by the firmware while keeping the
// state machine free of Pico SDK dependencies.
static const power_monitor_limits_t LIMITS = {
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

// Simulate a constant measurement and return the first one-based sample that
// trips, or -1 if the supplied test horizon is exhausted.
static int samples_until_trip(float power_w, float current_a,
			      bool current_overrange, float sample_time_s)
{
	power_monitor_state_t state;
	power_monitor_reset(&state);

	for (int samples = 1; samples < 10000; ++samples) {
		if (power_monitor_update(&state, &LIMITS, power_w, current_a,
					 current_overrange, sample_time_s) !=
		    POWER_TRIP_NONE)
			return samples;
	}

	return -1;
}

// Sustained overload duration must scale inversely with watts above 20 W.
static bool test_power_trip_times(void)
{
	const int samples_at_21_w =
		samples_until_trip(21.0f, 1.0f, false, SAMPLE_TIME_S);
	const int samples_at_24_w =
		samples_until_trip(24.0f, 1.0f, false, SAMPLE_TIME_S);
	const int samples_at_30_w =
		samples_until_trip(30.0f, 1.0f, false, SAMPLE_TIME_S);

	CHECK(samples_at_21_w >= 40 && samples_at_21_w <= 41);
	CHECK(samples_at_24_w >= 10 && samples_at_24_w <= 11);
	CHECK(samples_at_30_w == 4);
	CHECK(samples_at_21_w > samples_at_24_w);
	CHECK(samples_at_24_w > samples_at_30_w);
	return true;
}

// Power debt must accumulate, recover at the configured cap, and clamp at zero.
static bool test_power_accumulation_and_cooling(void)
{
	power_monitor_state_t state;
	power_monitor_reset(&state);

	for (int i = 0; i < 5; ++i)
		CHECK(power_monitor_update(&state, &LIMITS, 24.0f, 1.0f,
					   false, SAMPLE_TIME_S) ==
		      POWER_TRIP_NONE);

	// At 10 W there is 10 W of unused headroom, but recovery is capped at
	// 4 J/s. A 50 ms interval therefore repays 0.2 J.
	CHECK(power_monitor_update(&state, &LIMITS, 10.0f, 1.0f, false,
				   0.050f) == POWER_TRIP_NONE);

	CHECK(state.power_overload_j > 1.79f);
	CHECK(state.power_overload_j < 1.81f);

	for (int i = 0; i < 5; ++i)
		CHECK(power_monitor_update(&state, &LIMITS, 24.0f, 1.0f,
					   false, SAMPLE_TIME_S) ==
		      POWER_TRIP_NONE);
	CHECK(power_monitor_update(&state, &LIMITS, 24.0f, 1.0f, false,
				   SAMPLE_TIME_S) == POWER_TRIP_POWER_OVERLOAD);

	power_monitor_reset(&state);
	for (int i = 0; i < 5; ++i)
		power_monitor_update(&state, &LIMITS, 24.0f, 1.0f, false,
				     SAMPLE_TIME_S);
	// The complete 2 J debt clears after 0.5 s at the 4 W recovery cap.
	power_monitor_update(&state, &LIMITS, 10.0f, 1.0f, false, 0.500f);
	CHECK(state.power_overload_j == 0.0f);
	return true;
}

// The quantized 2.3 A boundary is continuous only while power remains safe.
static bool test_2_3_a_is_continuous(void)
{
	power_monitor_state_t state;
	power_monitor_reset(&state);

	// Use the quantized production boundary for an hour of simulated runtime.
	for (int i = 0; i < 36000; ++i)
		CHECK(power_monitor_update(
			      &state, &LIMITS, 18.0f, CONTINUOUS_CURRENT_LIMIT_A,
			      false, SAMPLE_TIME_S) == POWER_TRIP_NONE);

	CHECK(state.current_overload_a_s == 0.0f);
	CHECK(state.current_overrange_continuous_s == 0.0f);
	CHECK(state.current_overrange_exposure_s == 0.0f);

	// The independent 20 W limit still governs at higher bus voltages.
	const int high_voltage_samples = samples_until_trip(
		24.0f, CONTINUOUS_CURRENT_LIMIT_A, false, SAMPLE_TIME_S);
	CHECK(high_voltage_samples >= 10 && high_voltage_samples <= 11);
	return true;
}

// Measurable current above 2.3 A consumes the bridge-current A*s budget.
static bool test_measurable_current_overload(void)
{
	const int samples_at_2_35_a =
		samples_until_trip(18.0f, 2.35f, false, 0.01f);
	const int samples_at_2_40_a =
		samples_until_trip(18.0f, 2.40f, false, 0.01f);

	CHECK(samples_at_2_35_a >= 100 && samples_at_2_35_a <= 102);
	CHECK(samples_at_2_40_a >= 50 && samples_at_2_40_a <= 51);
	CHECK(samples_at_2_35_a > samples_at_2_40_a);

	power_monitor_state_t state;
	power_monitor_reset(&state);
	CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 2.40f, false,
				   0.050f) == POWER_TRIP_NONE);
	CHECK(state.current_overload_a_s > 0.004f);
	CHECK(state.current_overload_a_s < 0.006f);

	// A quiet period restores the complete budget used by the 50 ms event.
	CHECK(power_monitor_update(&state, &LIMITS, 10.0f, 1.0f, false,
				   0.300f) == POWER_TRIP_NONE);
	CHECK(state.current_overload_a_s == 0.0f);
	return true;
}

// Closely spaced short current events must retain enough debt to trip.
static bool test_repeated_current_events_accumulate(void)
{
	power_monitor_state_t state;
	power_monitor_reset(&state);

	// A rapid 2.35 A pulse train retains some heat between events.
	for (int cycle = 0; cycle < 30; ++cycle) {
		CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 2.35f,
					   false, 0.050f) == POWER_TRIP_NONE);
		CHECK(power_monitor_update(&state, &LIMITS, 10.0f, 1.0f,
					   false, 0.050f) == POWER_TRIP_NONE);
	}
	CHECK(state.current_overload_a_s > 0.04f);

	for (int cycle = 0; cycle < 10 &&
			    state.trip_reason == POWER_TRIP_NONE;
	     ++cycle) {
		power_monitor_update(&state, &LIMITS, 18.0f, 2.35f, false,
				     0.050f);
		power_monitor_update(&state, &LIMITS, 10.0f, 1.0f, false,
				     0.050f);
	}
	CHECK(state.trip_reason == POWER_TRIP_CURRENT_OVERLOAD);
	return true;
}

// Validate the separate continuous and leaky limits for unknown peak current.
static bool test_current_sensor_overrange_policy(void)
{
	power_monitor_state_t state;
	power_monitor_reset(&state);

	// One maximum-length solenoid event is explicitly allowed.
	CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 2.40f, true,
				   0.050f) == POWER_TRIP_NONE);
	CHECK(state.current_overrange_continuous_s > 0.049f);
	CHECK(state.current_overrange_exposure_s > 0.049f);

	// Sufficient quiet time clears both continuous and accumulated exposure.
	CHECK(power_monitor_update(&state, &LIMITS, 10.0f, 1.0f, false,
				   0.250f) == POWER_TRIP_NONE);
	CHECK(state.current_overrange_continuous_s == 0.0f);
	CHECK(state.current_overrange_exposure_s == 0.0f);

	// A continuously clipped signal exceeds the 80 ms event allowance.
	CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 2.40f, true,
				   0.050f) == POWER_TRIP_NONE);
	CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 2.40f, true,
				   0.031f) ==
	      POWER_TRIP_CURRENT_SENSOR_OVERRANGE);

	// Trip state remains latched even after a long quiet interval.
	CHECK(power_monitor_update(&state, &LIMITS, 0.0f, 0.0f, false,
				   10.0f) ==
	      POWER_TRIP_CURRENT_SENSOR_OVERRANGE);

	// Separated overrange events also consume a leaky exposure budget.
	power_monitor_reset(&state);
	for (int cycle = 0; cycle < 2; ++cycle) {
		CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 2.40f, true,
					   0.050f) == POWER_TRIP_NONE);
		CHECK(power_monitor_update(&state, &LIMITS, 10.0f, 1.0f,
					   false, 0.050f) == POWER_TRIP_NONE);
	}
	CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 2.40f, true,
				   0.050f) ==
	      POWER_TRIP_CURRENT_SENSOR_OVERRANGE);
	return true;
}

// Brief high-power events use energy budget, and the resulting trip stays
// latched.
static bool test_large_power_pulses_use_budget_and_latch(void)
{
	power_monitor_state_t state;
	power_monitor_reset(&state);

	// At the high-voltage end, one physically plausible 50 ms, 55 W event is
	// allowed while consuming 1.75 J of the 4 J power budget.
	CHECK(power_monitor_update(&state, &LIMITS, 55.0f,
				   CONTINUOUS_CURRENT_LIMIT_A, false,
				   0.050f) == POWER_TRIP_NONE);
	CHECK(state.power_overload_j > 1.74f);
	CHECK(state.power_overload_j < 1.76f);

	// A second event is still valid; a third without cooling reaches the budget.
	CHECK(power_monitor_update(&state, &LIMITS, 55.0f,
				   CONTINUOUS_CURRENT_LIMIT_A, false,
				   0.050f) == POWER_TRIP_NONE);
	CHECK(power_monitor_update(&state, &LIMITS, 55.0f,
				   CONTINUOUS_CURRENT_LIMIT_A, false,
				   0.050f) == POWER_TRIP_POWER_OVERLOAD);
	CHECK(power_monitor_update(&state, &LIMITS, 0.0f, 0.0f, false,
				   SAMPLE_TIME_S) == POWER_TRIP_POWER_OVERLOAD);
	return true;
}

// Exact continuous boundaries are neutral, and cooling cannot make debt
// negative.
static bool test_thresholds_and_zero_clamp(void)
{
	power_monitor_state_t state = {
		.power_overload_j = 0.01f,
		.current_overload_a_s = 0.001f,
		.current_overrange_continuous_s = 0.0f,
		.current_overrange_exposure_s = 0.001f,
		.trip_reason = POWER_TRIP_NONE,
	};

	CHECK(power_monitor_update(&state, &LIMITS, MAX_POWER_TARGET,
				   CONTINUOUS_CURRENT_LIMIT_A, false,
				   SAMPLE_TIME_S) == POWER_TRIP_NONE);
	CHECK(state.power_overload_j == 0.01f);
	CHECK(state.current_overload_a_s == 0.001f);
	CHECK(state.current_overrange_exposure_s == 0.0f);

	CHECK(power_monitor_update(&state, &LIMITS, 10.0f, 1.0f, false,
				   1.0f) == POWER_TRIP_NONE);
	CHECK(state.power_overload_j == 0.0f);
	CHECK(state.current_overload_a_s == 0.0f);
	return true;
}

// A representative musical cycle recovers while >20 W-average cycles still
// trip.
static bool test_power_pulse_train_recovery_and_accumulation(void)
{
	power_monitor_state_t state;
	power_monitor_reset(&state);

	// This log-shaped musical cycle averages 16.75 W. Actual headroom below
	// 20 W must repay the short 25 W event instead of accumulating forever.
	for (int cycle = 0; cycle < 10000; ++cycle) {
		CHECK(power_monitor_update(&state, &LIMITS, 25.0f, 1.0f,
					   false, 0.025f) == POWER_TRIP_NONE);
		CHECK(power_monitor_update(&state, &LIMITS, 18.0f, 1.0f,
					   false, 0.025f) == POWER_TRIP_NONE);
		CHECK(power_monitor_update(&state, &LIMITS, 12.0f, 1.0f,
					   false, 0.050f) == POWER_TRIP_NONE);
		CHECK(state.power_overload_j == 0.0f);
	}

	// A 20.5 W average cycle still gains 0.05 J each time and must
	// eventually trip; recovery never exceeds the real headroom below 20 W.
	power_monitor_reset(&state);
	int trip_cycle = -1;
	for (int cycle = 1; cycle <= 100; ++cycle) {
		const power_trip_reason_t reason = power_monitor_update(
			&state, &LIMITS, 25.0f, 1.0f, false, 0.050f);
		if (reason != POWER_TRIP_NONE) {
			CHECK(reason == POWER_TRIP_POWER_OVERLOAD);
			trip_cycle = cycle;
			break;
		}
		CHECK(power_monitor_update(&state, &LIMITS, 16.0f, 1.0f,
					   false, 0.050f) == POWER_TRIP_NONE);
	}
	CHECK(trip_cycle >= 76 && trip_cycle <= 77);

	// Headroom smaller than the 4 W cap must be credited at its actual value.
	// This 20.45 W average waveform therefore still gains about 0.045 J/cycle.
	power_monitor_reset(&state);
	trip_cycle = -1;
	for (int cycle = 1; cycle <= 100; ++cycle) {
		const power_trip_reason_t reason = power_monitor_update(
			&state, &LIMITS, 21.0f, 1.0f, false, 0.050f);
		if (reason != POWER_TRIP_NONE) {
			CHECK(reason == POWER_TRIP_POWER_OVERLOAD);
			trip_cycle = cycle;
			break;
		}
		CHECK(power_monitor_update(&state, &LIMITS, 19.9f, 1.0f,
					   false, 0.050f) == POWER_TRIP_NONE);
	}
	CHECK(trip_cycle >= 89 && trip_cycle <= 90);
	return true;
}

// Equivalent elapsed time must produce equivalent debt regardless of sampling.
static bool test_power_elapsed_time_segmentation(void)
{
	static const float segments_s[] = {
		0.001f, 0.007f, 0.042f, 0.200f, 0.500f,
	};
	power_monitor_state_t bulk;
	power_monitor_state_t split;
	power_monitor_reset(&bulk);
	power_monitor_reset(&split);

	CHECK(power_monitor_update(&bulk, &LIMITS, 24.0f, 1.0f, false,
				   0.750f) == POWER_TRIP_NONE);
	for (size_t i = 0; i < sizeof(segments_s) / sizeof(segments_s[0]);
	     ++i)
		CHECK(power_monitor_update(&split, &LIMITS, 24.0f, 1.0f,
					   false, segments_s[i]) == POWER_TRIP_NONE);
	CHECK(bulk.power_overload_j > 2.999f &&
	      bulk.power_overload_j < 3.001f);
	CHECK(split.power_overload_j > 2.999f &&
	      split.power_overload_j < 3.001f);

	// Both actual-headroom recovery and capped recovery are independent of
	// how a constant-power interval is divided into samples.
	CHECK(power_monitor_update(&bulk, &LIMITS, 18.0f, 1.0f, false,
				   0.750f) == POWER_TRIP_NONE);
	for (size_t i = 0; i < sizeof(segments_s) / sizeof(segments_s[0]);
	     ++i)
		CHECK(power_monitor_update(&split, &LIMITS, 18.0f, 1.0f,
					   false, segments_s[i]) == POWER_TRIP_NONE);
	CHECK(bulk.power_overload_j > 1.499f &&
	      bulk.power_overload_j < 1.501f);
	CHECK(split.power_overload_j > 1.499f &&
	      split.power_overload_j < 1.501f);

	CHECK(power_monitor_update(&bulk, &LIMITS, 10.0f, 1.0f, false,
				   0.375f) == POWER_TRIP_NONE);
	for (size_t i = 0; i < sizeof(segments_s) / sizeof(segments_s[0]);
	     ++i)
		CHECK(power_monitor_update(&split, &LIMITS, 10.0f, 1.0f,
					   false, segments_s[i] * 0.5f) ==
		      POWER_TRIP_NONE);
	CHECK(bulk.power_overload_j == 0.0f);
	CHECK(split.power_overload_j == 0.0f);
	return true;
}

// Raw ADC thresholds must preserve their intended engineering-unit ordering.
static bool test_raw_thresholds_match_engineering_units(void)
{
	const float continuous_current_a =
		(CONTINUOUS_CURRENT_RAW_LIMIT * I_SENSE_SACLER) / 1000.0f;
	const float previous_current_a =
		((CONTINUOUS_CURRENT_RAW_LIMIT - 1) * I_SENSE_SACLER) /
		1000.0f;

	CHECK(continuous_current_a >= MAX_CURRENT_TARGET);
	CHECK(previous_current_a < MAX_CURRENT_TARGET);
	CHECK(CONTINUOUS_CURRENT_RAW_LIMIT < CURRENT_OVERRANGE_EXIT_RAW);
	CHECK(CURRENT_OVERRANGE_EXIT_RAW < CURRENT_OVERRANGE_ENTER_RAW);
	CHECK(CURRENT_OVERRANGE_ENTER_RAW <= ADC_MAX_VAL);
	return true;
}

// Run in short-circuit order so the first failure remains the clearest result.
int main(void)
{
	const bool passed = test_power_trip_times() &&
			    test_power_accumulation_and_cooling() &&
			    test_2_3_a_is_continuous() &&
			    test_measurable_current_overload() &&
			    test_repeated_current_events_accumulate() &&
			    test_current_sensor_overrange_policy() &&
			    test_large_power_pulses_use_budget_and_latch() &&
			    test_thresholds_and_zero_clamp() &&
			    test_power_pulse_train_recovery_and_accumulation() &&
			    test_power_elapsed_time_segmentation() &&
			    test_raw_thresholds_match_engineering_units();

	if (!passed)
		return 1;

	puts("power monitor tests passed");
	return 0;
}
