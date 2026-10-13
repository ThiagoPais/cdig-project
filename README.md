# Open-loop duty sweep for a resistive thermal load (ESP32)

Firmware for an ESP32 that heats a 50 ohm resistive load (three 150 ohm resistors in parallel, 12 V,
switched by a TIP120 from a PWM pin) and finds the PWM duty at which the load settles at 55, 60 and
65 degrees C. Two LM35 sensors measure the load and the ambient temperature. The sweep is open loop:
it steps the duty up, waits for each step to settle, and interpolates the duty for each target.

## Wiring

Labels are the ones printed on the DOIT-style DevKit board.

| Function | Board pin | GPIO |
| --- | --- | --- |
| Load LM35 output | D33 | GPIO33 (ADC1_CH5) |
| Ambient LM35 output | D34 | GPIO34 (ADC1_CH6, input only) |
| PWM to the TIP120 base | D14 | GPIO14 |

LM35 caveat: the sensors are powered at 3.3 V, below the 4 V minimum of the datasheet. Readings may be
wrong at the higher temperatures (the datasheet's minimum-supply chart suggests trouble near 50 degrees C
and above). Use a multimeter at the sensor output (10 mV per degree C) to compare if in doubt.

## Build, test and upload

`pio` is the PlatformIO CLI (with the VS Code extension it lives in `~/.platformio/penv/bin`).

```
pio run                      # build the firmware
pio test -e native           # host unit tests (no board needed)
pio run -t upload            # flash the board
pio device monitor           # serial monitor, 115200 baud
```

The serial monitor is configured with `monitor_dtr = 0` and `monitor_rts = 0` to avoid a reset on some boards when the monitor is reopened in the middle of a sweep (not yet verified on this board). Power the board from USB during a sweep and do not unplug it.

## Serial commands (115200 baud, newline-terminated, case-sensitive)

| Command | Effect |
| --- | --- |
| `start` | start a sweep from 0 % duty |
| `start N` | start from N % duty (0 to 25), then N + 2.5, N + 5 ... up to 25 % |
| `stop` | stop the sweep, duty 0 % |
| `results` | print the table so far, and the targets if the sweep finished |
| `temp` | print the latest load and ambient temperature, raw ADC count and sample count |

Nothing heats until `start`. A start is refused if the load is more than 3.0 degrees C from the ambient
temperature (let it cool first) or after a cutoff (reset the board).

## What a sweep does

1. Holds 0 % until the load settles, then computes the offset between the two sensors
   (mean of load minus ambient over the settle window) and applies it to the load temperature afterwards.
2. Applies each duty step (2.5 % increments, at most 25 %), waiting until the load temperature changes
   by at most 0.5 degrees C (highest minus lowest of the per-second values) over 2 minutes (a step that does not settle in 45 minutes is recorded as
   `no`).
3. Prints a log line every 5 seconds, then a table and the interpolated duty for 55, 60 and 65 degrees C.

Expected duration: about 22 minutes at the shortest (the 0 % hold plus 10 steps, 2 minutes each) up to
about 8.25 hours (11 windows of 45 minutes). Starting above 0 % skips the lower steps, but the initial
0 % hold always runs first (at least one settle window).

Example output lines:

```
READY
(before the offset is known, the load temperature in LOG lines is uncorrected)
LOG t=5 duty=0.00% (0) load=25.3 C ambient=25.0 C load_adc=941.2 ambient_adc=930.7
TABLE offset=0.75 C
duty_pct duty_count load_C ambient_C load_adc ambient_adc settled
2.49 102 31.2 25.1 1189.4 934.5 yes
TARGET 60.0 C: duty=17.5 % (717 counts)
```

## Safety

- The duty never exceeds 25 % (1023 of 4095 counts, `kCapCounts` in `lib/sweep/include/duty.h`): about
  0.24 W per resistor, just under the 1/4 W rating.
- If the uncorrected load temperature reaches 80 degrees C, or a sensor's window-mean count is 0 or 4095
  (short or saturation), the duty goes to 0 % and stays there until the board is reset.
- A disconnected (floating) sensor is not detected; only the duty cap protects the resistors then.
- The PWM pin may glitch for a moment while the board boots or resets (accepted).

## Tunable constants

In `lib/sweep/include/SweepConfig.h`: settle threshold (0.5 degrees C), settle window (120 s), step
timeout (2700 s), step size (2.5 %), last step (`kCapPercent`, 25 %: where the sweep stops), cutoff
(80 degrees C), start tolerance (3.0 degrees C), log period (5 s), PWM frequency (1 kHz) and the target
temperatures.

Other limits live elsewhere:
- the hard duty cap `kCapCounts` (1023 counts) in `lib/sweep/include/duty.h`; raising `kCapPercent` alone
  does not raise it;
- the largest accepted `start N` (25) in `lib/sweep/src/commands.cpp`;
- the settle window cannot exceed `kSettleBufferSize` (600 s, also in `SweepConfig.h`);
- the table holds at most `kMaxRows` (12) rows (`lib/sweep/include/sweep.h`), and rows beyond that are
  dropped silently. A sweep from 0 % records 1 + 25 / step rows (the 0 % row plus one per step), so the
  step size must stay above about 2.1 % (25 % / 12 rows).

## Layout

- `lib/measure`: count to degrees C, the 0.8 s window mean per 1 s period (reusable on its own).
- `lib/sweep`: duty cap, command parser, the sweep state machine (settle, offset, table, interpolation).
- `src/main.cpp`: Arduino glue (ADC, PWM, serial).
- `test/`: host unit tests (Unity, `pio test -e native`).
- `specs/001-duty-sweep/`: specification, plan, tasks and progress of this feature.
