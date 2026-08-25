# Kassiopeia power protection handoff

Status: experimental bench-tuned implementation, not production-calibrated  
Last updated: 2026-08-08

## Executive summary

The old firmware shut the device down after one power reading above 20 W. The
new implementation uses elapsed-time accumulators so short musical actuator
events are allowed while sustained or high-duty loading still shuts down.

There are three independent protection lanes:

1. post-bridge electrical power above 20 W;
2. measurable bridge current above 2.3 A;
3. current-sensor overrange, where current amplitude is no longer known.

The first lane to reach its limit latches the shutdown. All four PWM outputs and
all four pulse outputs are forced low, the PWM interrupt is gated so MIDI or CV
state cannot re-enable them, and the power LED blinks until reboot.

### Current hard-fault timing

From a cold/empty protection state, a continuously recognized current-sensor
overrange trips after **80 ms**, provided the calibrated raw channel actually
reaches and remains in the overrange state:

```c
#define CURRENT_OVERRANGE_CONTINUOUS_LIMIT_S 0.080f
```

Pre-existing exposure, current debt, or power debt can shut down sooner. The
separate **120 ms** value is a leaky budget for repeated, separated
overrange events. It does not allow one continuous fault to last 120 ms.
Starting with zero retained exposure, a single event encounters the 80 ms limit
first.

The 80 ms is integrated time after the firmware recognizes overrange. Actual
wall-clock shutdown also includes ADC averaging, detection, one main-loop
sampling interval, and any synchronous main-loop stall. Those additions are
normally millisecond-scale, but debug output does not have a formal whole-line
latency bound. This is not a safety-rated deadline; verify it with diagnostics
disabled and an oscilloscope on production hardware.

There is deliberately no immediate 35 W or 2.3 A trip in this revision. Once
the current monitor saturates, firmware knows only that current is at least the
reported value. If the averaged raw reading reaches ENTER and remains
hysteretically overrange, firmware permits 80 ms of recognized exposure before
shutdown. This is not a physical fault-to-output-low guarantee: if the real
analog plateau never reaches ENTER, the slower measurable-current/power lanes
or hardware protection govern instead. Hardware current limiting, the PTCs,
PCB/bridge surge capability, and worst-case adapter current must make the
recognized-exposure interval safe.

## Files and responsibilities

| File | Responsibility |
| --- | --- |
| `system/inc/config.h` | User-tunable thresholds, budgets, recovery rates, ADC scalers, and diagnostic settings. |
| `system/inc/power_monitor.h` | Pico-independent public API, limits, state, units, and trip reasons. |
| `system/src/power_monitor.c` | Pure elapsed-time accumulator and trip logic. No ADC, GPIO, timing, or logging dependencies. |
| `main.c` | Sequential ADC sampling, raw overrange hysteresis, real elapsed-time integration, diagnostics, trip latch, output shutdown, and LED blinking. |
| `tests/test_power_monitor.c` | Native host regression tests for thresholds, timing, recovery, pulse trains, latching, and sampling segmentation. |
| `tests/CMakeLists.txt` | Standalone native test target, separate from the ARM firmware build. |
| `CMakeLists.txt` | Firmware target and the 2 ms per-chunk USB no-progress timeout used by bench diagnostics. |

## Measurement semantics

Voltage and current are measured **after the input diode bridge**. Therefore:

```text
P_bus = V_bus * I
```

The 20 W target currently means 20 W on the measured post-bridge bus. DC-jack
power is higher by bridge dissipation. If the product specification is intended
to mean 20 W at the jack, the target/model must be adjusted; changing only the
documentation would be incorrect.

Current is the series current through the bridge and load. With the present
nominal scaler:

```text
I = current_raw * 0.5923999023 mA
V = voltage_raw * 0.006284179688 V
```

The theoretical current-monitor full scale is approximately 2.426 A. Component
tolerance, offset, ADC/reference error, temperature, and the real analog output
plateau must be measured; the nominal equations are not a calibration.

## Runtime data flow

1. `main.c` averages 255 current ADC conversions and then 255 voltage ADC
   conversions. Very short spikes can be attenuated, and current/voltage edges
   are slightly time-skewed because the channels are sampled sequentially.
2. `process_power_sample()` converts them to A, V, and W.
3. Raw hysteresis decides whether the current monitor is in overrange.
4. The previous measurement is integrated over the real time for which it was
   the latest sample. This zero-order-hold approach captures 20–50 ms events and
   does not assume the 100 ms serial-report period is the protection cadence.
5. `power_monitor_update()` advances all three protection lanes and returns a
   latched trip reason when a limit is reached.
6. `latch_power_trip()` disables interrupts briefly, sets `power_shut_down`,
   and forces PWM and pulse outputs low.
7. `process_power_sample()` prints one frozen trip snapshot after the first
   transition.
8. `on_pwm_wrap()` checks the latch before applying stored MIDI/CV levels or
   handling triggers. `on_uart_rx()` continues draining bytes but discards MIDI,
   and the MIDI-learn alarm exits, preventing output re-activation.

The operational latch is cleared only by rebooting. Calling
`power_monitor_reset()` clears the pure model state, but a future runtime re-arm
feature would also need to clear the hardware shutdown latch safely.

Power sampling and periodic diagnostics continue after shutdown, but
`power_monitor_update()` freezes all protection state once its trip reason is
latched. The power LED first toggles at the next 100 ms reporting boundary and
then approximately every 100 ms, producing a roughly 5 Hz full blink cycle.

Configuration flash writes separately mask interrupts before forcing every
output low, preventing an output-producing IRQ from reasserting a pin during
the flash-programming pause.

## Protection equations and current defaults

All accumulators clamp at zero, so idle time cannot bank unlimited credit.
Exact continuous boundaries are neutral: they neither add nor remove debt.

### 1. Post-bridge power

Current defaults:

```c
#define MAX_POWER_TARGET 20.0f
#define POWER_OVERLOAD_BUDGET_J 4.0f
#define POWER_OVERLOAD_RECOVERY_CAP_W 4.0f
```

For elapsed interval `dt`:

```text
if P > 20 W:
    power_debt += (P - 20 W) * dt

if P < 20 W:
    recovery_rate = min(20 W - P, 4 W)
    power_debt = max(0, power_debt - recovery_rate * dt)

trip when power_debt >= 4 J
```

Cold-state sustained-overload time is:

```text
t_trip = POWER_OVERLOAD_BUDGET_J / (P - MAX_POWER_TARGET)
```

Examples with the current 4 J budget:

| Constant measured power | Approximate trip time |
| ---: | ---: |
| 21 W | 4.0 s |
| 24 W | 1.0 s |
| 30 W | 0.4 s |
| 55 W | 0.114 s |

The 4 W recovery cap means a full 4 J bucket needs at least one second to clear,
even at idle. At 18 W it needs two seconds; at 19 W it needs four seconds.
Recovery never exceeds actual headroom below 20 W, so a repeating waveform with
true average power above 20 W must eventually accumulate debt.

The cap is intentionally conservative: a waveform with a literal average below
20 W can still accumulate if it relies on more than 4 W of deep low-power
headroom to balance dense high-power events.

### 2. Measurable bridge current

Current defaults:

```c
#define MAX_CURRENT_TARGET 2.3f
#define CURRENT_OVERLOAD_BUDGET_A_S 0.05f
#define CURRENT_OVERLOAD_DECAY_A 0.02f
```

The 2.3 A boundary is rounded upward to raw ADC count 3883, giving a nominal
effective boundary of approximately 2.300289 A. This avoids slowly accumulating
debt solely because 2.3 A falls between ADC counts.

```text
if I > I_continuous:
    current_debt += (I - I_continuous) * dt

if I < I_continuous:
    current_debt = max(0, current_debt - 0.02 A * dt)

trip when current_debt >= 0.05 A*s
```

Approximate cold-state examples:

| Constant measured current | Approximate trip time |
| ---: | ---: |
| 2.31 A | 5.15 s |
| 2.35 A | 1.01 s |
| 2.3998 A (raw 4051, just below overrange ENTER) | 0.50 s |

A full current bucket clears in 2.5 s below the continuous boundary. This is a
simple bridge-heating proxy: for a fixed diode forward drop, retained excess
bridge energy is proportional to `2 * V_f * current_debt`. It is not a complete
diode junction-temperature model. Once raw current reaches overrange ENTER, the
80 ms overrange lane normally trips before this measurable-current estimate.

### 3. Current-sensor overrange

Current defaults:

```c
#define CURRENT_OVERRANGE_ENTER_RAW 4052
#define CURRENT_OVERRANGE_EXIT_RAW 4018
#define CURRENT_OVERRANGE_CONTINUOUS_LIMIT_S 0.080f
#define CURRENT_OVERRANGE_BUDGET_S 0.120f
#define CURRENT_OVERRANGE_DECAY_S_PER_S 0.25f
```

Nominal conversions are approximately:

```text
ENTER 4052 -> 2.4004 A
EXIT  4018 -> 2.3803 A
ADC   4095 -> 2.4259 A theoretical full scale
```

Raw hysteresis enters overrange at or above 4052 and remains there until a
sample is at or below 4018. After entry, samples inside the 4019–4051 band still
count as overrange time.

While overrange:

```text
continuous_time += dt
exposure_time   += dt
```

Below overrange:

```text
continuous_time = 0
exposure_time = max(0, exposure_time - 0.25 * dt)
```

Trip occurs when either:

```text
continuous_time >= 80 ms
exposure_time   >= 120 ms
```

From an empty state, one 50 ms overrange event is therefore allowed if neither
the power nor measurable-current lane trips first. It takes 200 ms below the
exit threshold to repay its 50 ms exposure. Repeated events with insufficient
cooling can reach the 120 ms exposure budget even though no individual event
reaches 80 ms. A steady pattern above approximately 20% overrange duty has
positive exposure drift with the current 0.25 recovery rate.

Trip-reason priority is:

1. current-sensor overrange;
2. measurable current overload;
3. power overload.

All lanes are updated each interval; priority only selects the reported reason
if more than one limit is reached in the same update.

## Changing the variables

Edit the constants in `system/inc/config.h`, rebuild, then run the host tests.
`POWER_MONITOR_LIMITS` in `main.c` maps those constants into the pure monitor.

| Variable | Increasing it does this | Main risk/tradeoff |
| --- | --- | --- |
| `MAX_POWER_TARGET` | Permits more continuous post-bridge power and reduces power debt for every sample. | Raises continuous stress everywhere; this changes the fundamental product envelope. |
| `POWER_OVERLOAD_BUDGET_J` | Allows a larger uninterrupted or clustered power burst. | Lengthens every sustained-overload trip time according to `budget / excess W`. |
| `POWER_OVERLOAD_RECOVERY_CAP_W` | Repays power debt faster during sufficiently low-power intervals. | Permits denser repeated bursts; does not change a cold sustained-overload trip. |
| `MAX_CURRENT_TARGET` | Permits more continuous bridge current. | Reduces already-small sensor headroom and increases bridge/PTC heating. |
| `CURRENT_OVERLOAD_BUDGET_A_S` | Allows more measurable current-time above the baseline. | Lengthens bridge-current overload exposure. |
| `CURRENT_OVERLOAD_DECAY_A` | Clears measurable current debt faster below the baseline. | Permits denser repeated high-current events. |
| `CURRENT_OVERRANGE_ENTER_RAW` | Waits closer to the ADC rail before declaring unknown current. | If set above the real analog plateau, firmware may never enter overrange. |
| `CURRENT_OVERRANGE_EXIT_RAW` | Leaves overrange sooner. | Too little hysteresis can chatter near the boundary. |
| `CURRENT_OVERRANGE_CONTINUOUS_LIMIT_S` | Allows one unknown-amplitude event to last longer. | Directly increases hard-fault energy before shutdown. |
| `CURRENT_OVERRANGE_BUDGET_S` | Allows more repeated overrange exposure. | Permits a higher duty cycle of unknown-amplitude events. |
| `CURRENT_OVERRANGE_DECAY_S_PER_S` | Clears repeated-event exposure faster. | Permits overrange events to repeat more frequently. |
| `V_SENSE_SACLER` / `I_SENSE_SACLER` | Changes conversion from ADC counts to engineering units. | Affects all displayed and accumulated V/I/W; recalibrate rather than tune for desired behavior. |
| `POWER_MON_READOUT_TIME` | Changes serial-report and shutdown-LED toggle cadence. | Does **not** change protection integration cadence. |
| `POWER_MONITOR_DEBUG_LOG` | `1` enables 10 Hz bench logging; `0` removes periodic lines. | Periodic USB output can perturb sampling slightly; the one-time trip line remains enabled. |

Recommended tuning order:

1. Confirm whether the 20 W specification is post-bridge or at the DC jack.
2. Calibrate voltage/current scalers and characterize the current-monitor plateau,
   noise, and gain across units and temperature.
3. Set continuous power/current boundaries from verified thermal limits.
4. Set overrange ENTER below the lowest observed saturation plateau but above
   the highest calibrated 2.3 A reading plus noise. Set EXIT with measured
   hysteresis margin.
5. Validate the 80 ms hard-fault interval before relaxing any musical-event
   budget.
6. Use `POWER_OVERLOAD_BUDGET_J` to admit a larger single legitimate cluster.
7. Use `POWER_OVERLOAD_RECOVERY_CAP_W` when a below-20 W musical phrase is safe
   but debt still ratchets upward between phrases.
8. Tune current and overrange budgets separately; do not loosen them to solve a
   power-bucket nuisance trip.

Change one policy dimension at a time and capture the fast sampled trace, bridge
temperature, ambient temperature, adapter voltage/current limit, and output
pattern used for the decision.

## Serial diagnostics

With `POWER_MONITOR_DEBUG_LOG=1`, a typical line is:

```text
11.7V @ ... mA -> ...W; avg/max .../...W; peak raw .../...mA OVERRANGE
(P ...J, excess/avail .../...J, ...A*s, clip .../...ms)
```

Fields:

- initial V/I/W: endpoint sample at report time;
- `avg/max`: time-integrated average and maximum sampled power in the reporting
  window;
- `peak raw`: largest averaged current ADC result in the reporting window;
- `OVERRANGE`: overrange was observed at least once in that window, not that it
  persisted for the full 100 ms;
- `P`: current power debt out of `POWER_OVERLOAD_BUDGET_J`;
- `excess/avail`: gross excess energy and available capped recovery in the
  reporting window. Available recovery can exceed debt actually removed after
  the bucket reaches zero;
- `A*s`: current debt out of `CURRENT_OVERLOAD_BUDGET_A_S`;
- `clip`: current continuous-overrange and repeated-exposure state snapshots in
  milliseconds. They are not interval maxima.

The current reading and interval peak are based on averaged ADC samples. Once
overrange is reached, the displayed amperage and calculated power are lower
bounds; the actual peak cannot be reconstructed in firmware.

The USB stdout no-progress timeout is reduced to 2 ms per output chunk in
`CMakeLists.txt`. This does not bound a complete `printf`: a long line can use
multiple chunks, progress restarts the timeout, and mutex acquisition/final
flush have separate behavior. An unread port may also receive a truncated line.
Disable periodic diagnostics for production timing validation.

## Latest bench evidence

An interactively supplied log of the four-solenoid 12 V pattern was analyzed
for approximately 125 seconds:

- mean interval power: 14.63 W;
- highest 100 ms interval average: 17.288 W;
- highest reported sampled power: 28.284 W;
- maximum reported power debt: 0.195 J / 4 J (4.9%);
- maximum reported current debt: 0.002 A*s / 0.05 A*s (4%);
- maximum reported overrange state: 13.2 ms continuous and 13.2 ms exposure;
- power and current debt repeatedly returned to zero and ended at zero.

This is good evidence that the present 4 J / 4 W policy admits that musical
pattern without long-term debt. It is not proof that the clipped current
amplitude or 80 ms hard-fault interval is safe at maximum ambient temperature.
The source log is not committed with this handoff, and hardware revision,
adapter/current limit, ambient temperature, exact event timing, and firmware
revision were not captured together. Archive those items with future validation
logs before treating a result as reproducible engineering evidence.

## Build and test

Firmware:

```sh
cmake -S . -B build
cmake --build build --target kassiopeia -j2
```

Output image:

```text
build/kassiopeia.uf2
```

Native host tests without polluting the repository:

```sh
cmake -S tests -B /tmp/kassiopeia-host-tests
cmake --build /tmp/kassiopeia-host-tests -j2
ctest --test-dir /tmp/kassiopeia-host-tests --output-on-failure
```

The tests cover sustained power timing, capped recovery, safe and unsafe pulse
trains, the quantized current boundary, measurable current accumulation,
continuous/repeated overrange, trip latching, zero clamps, and elapsed-time
segmentation.

The native suite tests the pure model. It does not replace hardware validation
of ADC calibration, analog saturation, output shutdown latency, bridge thermal
behavior, PTC behavior, or the actual PWM/pulse pins.

## Production sign-off checklist

- Calibrate voltage and current against traceable instruments.
- Characterize the analog current plateau and noise across boards and
  temperature; confirm ENTER is reachable during every overcurrent condition.
- Test the 9 V/high-current case and the high-voltage actuator-pulse case.
- Scope fault onset to all PWM/pulse pins low; verify the worst observed latency.
- Verify active MIDI, CV, and trigger state cannot restore an output after trip.
- Run the intended worst musical patterns at maximum ambient until temperatures
  stabilize.
- Validate a worst-case short with the strongest supported adapter and confirm
  bridge, switches, PCB, connectors, and PTCs tolerate the pre-shutdown energy.
- Repeat with periodic USB diagnostics disabled.
- Treat the present constants as experimental until these measurements are
  recorded and reviewed.
