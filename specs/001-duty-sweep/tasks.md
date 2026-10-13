Status: Draft

# Tasks: Open-loop duty sweep for the thermal load

Source: spec.md (Status: Approved (Thiago Cardoso, 2026-10-12)), plan.md (Status: Approved (Thiago Cardoso, 2026-10-13))
Overrides: tasks.md executed while Status: Draft, at the user's explicit instruction in chat (2026-10-13); human tasks T10 and T11 are left pending and T12/T13 run without them (user: prioritize finishing)

Legend: [ ] todo, [x] done. Only the executor ticks boxes, after validation.
Metadata: `Parallel: yes|no` (safe to run alongside other tasks of the same wave), `Owner: agent|human` (human also when the only check is `manual`).
Tests: every task that covers an AC/E has its test files in `Files`; a docs-only or infrastructure task with no AC/E states `Tests: none (...)` and the executor skips test-writer for it.
Verify commands: test `pio test -e native` (exists after T1; `-f <folder>` filters one test folder), lint none, typecheck none (the compiler is the check), build `pio run`, verify all `pio run && pio test -e native`. `pio` is not on PATH in the discovery shell; it is `~/.platformio/penv/bin/pio` (VS Code PlatformIO install).
Commit convention: short imperative subject with the task ID, e.g. `T3: add measurement window average`.

## Execution waves

| Wave | Tasks | Notes |
| --- | --- | --- |
| 1 | T1 | harness; everything depends on it |
| 2 | T2, T3, T4 | `lib/sweep` duty, `lib/measure`, command parser: disjoint files |
| 3 | T5 | sweep shell and safety (uses T2, T3, T4) |
| 4 | T6 | same files as T5 |
| 5 | T7 | same files |
| 6 | T8 | same files |
| 7 | T9 | firmware glue |
| 8 | T10 | human: flash and quick hardware checks |
| 9 | T11 | human: long hardware checks |
| 10 | T12 | full verification |
| 11 | T13 | README |

## Milestone 1 (M1): Harness and building blocks

- [x] **T1: Create the native test environment**
  - Covers: none (unblocks every other task; adds the missing test layer)
  - Tests: none (infrastructure; a smoke test proves the harness)
  - Depends on: none
  - Files: `platformio.ini` (modify), `test/test_smoke/test_smoke.cpp` (new)
  - Done when: `pio test -e native` passes with the smoke test and `pio run` still builds the firmware env; `platformio.ini` has `default_envs = esp32doit-devkit-v1`, `[env:native]` with `platform = native`, `test_framework = unity`, a `build_src_filter` excluding `src/main.cpp`, and in the firmware env `platform = espressif32@7.1.3`, `monitor_speed = 115200`, `monitor_dtr = 0`, `monitor_rts = 0`.
  - Parallel: no
  - Owner: agent

- [x] **T2: Add duty conversion, cap clamp and tunables**
  - Covers: AC10 (cap enforcement), E8
  - Depends on: T1
  - Files: `lib/sweep/include/SweepConfig.h` (new), `lib/sweep/include/duty.h` (new), `lib/sweep/src/duty.cpp` (new), `test/test_duty/test_duty.cpp` (new)
  - Done when: `pio test -e native -f test_duty` passes, and the tests show `clampDuty` returning 1023 for 1024, 4095 and 65535, `percentToCounts(25.0)` equal to 1023, `percentToCounts(2.5)` equal to 102, and `SweepConfig.h` holds the constants of D12.
  - Parallel: yes
  - Owner: agent

- [x] **T3: Add the measurement unit (conversion, window mean, period scheduler)**
  - Covers: AC12, AC14
  - Depends on: T1
  - Files: `lib/measure/include/measure.h` (new), `lib/measure/src/measure.cpp` (new), `test/test_measure/test_measure.cpp` (new)
  - Done when: `pio test -e native -f test_measure` passes, and the tests show `countToCelsius` within 0.1 degrees C for the counts of 0.250, 0.550, 0.600, 0.650 and 0.800 V at 1.1 V full scale, the window mean of injected samples, `samples = 0` with held values for an empty period, `takeSampleNow` true only inside the first 800 ms at 2 ms spacing, and exactly one period result per 1000 ms.
  - Parallel: yes
  - Owner: agent

- [x] **T4: Add the serial command parser**
  - Covers: AC2 (command forms), E4, E12, E13
  - Depends on: T1
  - Files: `lib/sweep/include/commands.h` (new), `lib/sweep/src/commands.cpp` (new), `test/test_commands/test_commands.cpp` (new)
  - Done when: `pio test -e native -f test_commands` passes, and the tests show `start`, `start 10`, `stop`, `results` and `temp` parsed (case-sensitive, spaces and CR trimmed, empty lines ignored), `start -1`, `start 26` and `start abc` returning `BadStart`, an unknown word returning `Unknown`, and two lines arriving back to back parsed in arrival order.
  - Parallel: yes
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 2 (M2): Sweep logic (host-tested)

- [x] **T5: Add the sweep shell and safety behavior**
  - Covers: AC1, AC2 (start tolerance), AC8, AC9, AC13, AC17, E1, E2, E3, E4, E11, E13
  - Depends on: T2, T3, T4
  - Files: `lib/sweep/include/sweep.h` (new), `lib/sweep/src/sweep.cpp` (new), `test/test_sweep/test_sweep.cpp` (new)
  - Done when: `pio test -e native -f test_sweep` passes, and the tests show the ready message listing `start`, `stop`, `results`, `temp` and the settle values, duty 0 until `start`, refusal of `start` when |load - ambient| > 3.0 (E3), `start` ignored while running (E1), refused after a cutoff (E2), `stop` a no-op with a message when not running (E11), cutoff at an uncorrected load of 80.0 within one period with the duty at 0 and latched, cutoff naming the sensor for a mean count of 0 or 4095, and `temp` printing temperature, mean count and sample count.
  - Parallel: no
  - Owner: agent

- [ ] **T6: Add the 0 % hold, steps, settle detection and offset**
  - Covers: AC2 (steps), AC3, AC4, AC15, E6, E7, E8
  - Depends on: T5
  - Files: `lib/sweep/src/sweep.cpp` (modify), `lib/sweep/include/sweep.h` (modify), `test/test_sweep/test_sweep.cpp` (modify), `test/test_sweep/sim_process.h` (new)
  - Done when: `pio test -e native -f test_sweep` passes, and the tests (driven by the simulated first-order process with injected time) show a sweep starting with the 0 % hold, the offset computed once the hold settles, steps `N + k*2.5` up to at most 25 % applied as 1023 counts for the last one, a step recorded as settled only when max - min of the last window is at most the threshold, a step recorded as "not settled" at 45 minutes, and a cutoff in the middle of a window not recording the step as settled.
  - Parallel: no
  - Owner: agent

- [ ] **T7: Add the result table, interpolation and `results`**
  - Covers: AC5, AC6, AC7, AC16, E5, E10, E14
  - Depends on: T6
  - Files: `lib/sweep/src/sweep.cpp` (modify), `lib/sweep/include/sweep.h` (modify), `test/test_sweep/test_sweep.cpp` (modify)
  - Done when: `pio test -e native -f test_sweep` passes, and the tests show the table with duty %, duty count, load and ambient temperatures, both raw counts, settled flag and the offset in the header; the interpolated duty for 55, 60 and 65 on a known linear process within 0.1 %; "not reachable" for a target outside the table and for fewer than two settled rows; the first bracketing pair on a non-monotonic table; `results` reprinting the table after a run whose output was discarded; and a second `start` discarding the old table.
  - Parallel: no
  - Owner: agent

- [ ] **T8: Add the periodic log line and a full simulated sweep**
  - Covers: AC11, AC10 (every period), G1 end to end
  - Depends on: T7
  - Files: `lib/sweep/src/sweep.cpp` (modify), `test/test_sweep/test_sweep.cpp` (modify), `test/test_sweep/sim_process.h` (modify)
  - Done when: `pio test -e native` passes (all test folders), and the tests show a log line every 5 seconds (+/- 1 s) with the time since `start`, duty % and counts, both temperatures and both raw counts; a complete simulated sweep from `start` to the printed targets in which `dutyCounts()` never exceeds 1023 on any period and the three reported duties match the simulated process within 0.5 %.
  - Parallel: no
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 3 (M3): Firmware and hardware checks

- [ ] **T9: Wire the firmware (LEDC, ADC, Serial, loop)**
  - Covers: AC1 (boot), AC10 (pin), AC14, AC17, E12 (on hardware)
  - Tests: none (hardware glue; verified by T10 and T11 and by the host tests of T2 to T8)
  - Depends on: T8
  - Files: `src/main.cpp` (modify)
  - Done when: `pio run` builds the firmware env without errors and `pio test -e native` still passes, and `src/main.cpp` sets D14 low first, configures LEDC channel 0 at 1 kHz with 12 bits on GPIO14, calls `clampDuty` in the only function that writes the duty, reads GPIO33 and GPIO34 at 0 dB attenuation with a sample every 2 ms inside the first 800 ms of each second, feeds `Sweep` with `onPeriod` once per second and resets the averagers, and reads `Serial` at 115200 one character at a time into the line buffer.
  - Parallel: no
  - Owner: agent

- [ ] **T10: Flash the board and run the quick hardware checks (M1, M2, M3)**
  - Covers: AC1, AC9, AC14, AC17
  - Tests: none (manual hardware procedure)
  - Depends on: T9
  - Files: none
  - Done when: `manual`: (1) run `pio run -t upload` with the board on USB and open the serial monitor at 115200; within 5 seconds the ready message appears and the PWM pin (D14) reads about 0 V (M1); (2) type `temp`: both temperatures are near room temperature, the mean counts are plausible (about 930 per 25 degrees C), and the sample count is at least 100 for each sensor (M2); (3) with the heater supply off, briefly connect the load sensor input (D33) to GND: a cutoff message naming the load sensor appears and `start` is then refused until reset (M3); report the observations.
  - Parallel: no
  - Owner: human

- [ ] **T11: Run the long hardware checks (M4, M5, M6)**
  - Covers: AC10 (at the pin), E9, E10
  - Tests: none (manual hardware procedure)
  - Depends on: T10
  - Files: none
  - Done when: `manual`: (1) run `start 20` with the load connected to the 12 V supply: log lines appear every 5 seconds and a multimeter on D14 shows an average near 25 % of 3.3 V at most (M6); (2) close and reopen the serial monitor mid-sweep (the board must not reset), then type `results`: the table so far is printed (M4); (3) press the board's reset button mid-sweep: the duty returns to 0 and the ready message appears, the sweep is lost (M5); report the observations.
  - Parallel: no
  - Owner: human

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 4 (M4): Validation and documentation sync

- [ ] **T12: Run full verification**
  - Covers: all AC, E
  - Depends on: T11
  - Files: none
  - Done when: `pio run && pio test -e native` passes, and every AC and E in the matrix below has a passing host test or a documented manual observation from T10 or T11.
  - Parallel: no
  - Owner: agent

- [ ] **T13: Write the README**
  - Covers: plan.md "Documentation impact" row marked `task` (README)
  - Tests: none (docs only)
  - Depends on: T12
  - Files: `README.md` (new)
  - Done when: `README.md` contains the pin map as wired (D33 load sensor, D34 ambient sensor, D14 PWM), the commands `start [N]`, `stop`, `results`, `temp` at 115200 baud, the tunable constants and where they live, the expected run duration (about 22 minutes to 8.25 hours), the 3.3 V LM35 caveat and the monitor DTR/RTS note, and the diff touches only `README.md`.
  - Parallel: no
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Out of tasks (needs approval)

- `CLAUDE.md` (SDD section) -> set `Test: pio test -e native` and `Verify (all): pio run && pio test -e native` once T1 is done.
- `docs/Prj1CDig.pdf` -> broken symlink (`../Prj1CDig.pdf` should point to `../../Prj1CDig.pdf`, or the file should be copied in); the user's file layout.

## Coverage matrix

| ID | Description (short) | Tasks |
| --- | --- | --- |
| AC1 | ready message, duty 0, waits for start | T5, T9, T10 |
| AC2 | ascending steps, `start [N]`, start tolerance | T4, T5, T6 |
| AC3 | settle by max - min over the window | T6 |
| AC4 | 45 min timeout, "not settled" | T6 |
| AC5 | result table | T7 |
| AC6 | interpolated duty | T7 |
| AC7 | target not reachable | T7 |
| AC8 | cutoff at 80, latched | T5 |
| AC9 | rail check cutoff | T5, T10 |
| AC10 | duty never above 1023 | T2, T8, T9, T11 |
| AC11 | log line every 5 s | T8 |
| AC12 | count to degrees C | T3 |
| AC13 | stop | T5 |
| AC14 | 0.8 s window mean | T3, T9, T10 |
| AC15 | offset from the 0 % hold | T6 |
| AC16 | `results` | T7 |
| AC17 | `temp` | T5, T9, T10 |
| E1 | start while running | T5 |
| E2 | start after cutoff | T5 |
| E3 | start refused when far from ambient | T5 |
| E4 | unknown command | T4, T5 |
| E5 | non-monotonic table | T7 |
| E6 | ambient changes during the sweep | T6 |
| E7 | cutoff during a settling window | T6 |
| E8 | rounding and clamp | T2, T6 |
| E9 | reset mid-sweep | T11 |
| E10 | host disconnected | T7, T11 |
| E11 | stop when not running | T5 |
| E12 | commands in arrival order | T4, T9 |
| E13 | invalid `start N` | T4, T5 |
| E14 | start after a finished sweep | T7 |
