Status: Approved (Thiago Cardoso, 2026-10-19)

# Tasks: Open-loop PRBS experiment and data log

Source: spec.md (Status: Approved (Thiago Cardoso, 2026-10-19)), plan.md (Status: Approved (Thiago Cardoso, 2026-10-19))
Overrides: none

Legend: [ ] todo, [x] done. Only the executor ticks boxes, after validation.
Metadata: `Parallel: yes|no` (safe to run alongside other tasks of the same wave), `Owner: agent|human` (human also when the only check is `manual`), `Blocking: yes|no` (human tasks only: `no` lets later independent agent tasks proceed), `Type: env-validation` (optional; failure reopens the spec).
Tests: every task that covers an AC/E has its test files in `Files`; otherwise `Tests: none (<reason>)` with reason in docs-only | infrastructure | hardware-glue | manual.
Verify commands: test `pio test -e native` (one folder: `pio test -e native -f test_<name>`), helper test `python3 -m unittest discover -s test/helper -t test/helper -v` (created by T2), lint none, typecheck none (the compiler with `-Wall` is the check), build `pio run`, verify all `pio run && pio test -e native && python3 -m unittest discover -s test/helper -t test/helper`. `pio` is on PATH here (`/usr/bin/pio`); otherwise `~/.platformio/penv/bin/pio`. Run host tests without `PLATFORMIO_BUILD_FLAGS` set (they assume the default 25 % cap, 1023 counts).
Commit convention: none established; short imperative subject with the task ID, e.g. `T6: add prbs commands to the parser`.

Conventions for every agent task: existing tests are never edited or weakened (the 156 tests of feature 001 must keep passing); new C++ tests use values derived from `kCapCounts`, never 1266 or 1490 (plan D13); the exact output texts are the ones pinned in plan.md "Data models and interfaces".

## Resolved flag (F1): helper internal interface

Resolved 2026-10-19: the user approved adding the helper's Python interface (`open_port(path) -> int`, `main(argv, timeouts=Timeouts()) -> int`, `Timeouts` fields `sync`, `ready`, `loaded`, `run_start`, `silence`) to plan.md D10. T2 and T15-T18 are written against it. The edit reset plan.md to Draft; the user re-approved it manually (plan.md line 1 reads `Approved (Thiago Cardoso, 2026-10-19)`), so the Source line above is correct.

## Execution waves

Waves are computed within a milestone (the executor stops at every checkpoint). Hardware tasks are `Parallel: no` because there is one board.

| Milestone | Wave | Tasks | Notes |
| --- | --- | --- | --- |
| M1 | 1 | T1, T2, T3 | disjoint files |
| M1 | 2 | T4 | human: A9 on the board; needs the probe (T1) and `open_port` (T2) |
| M1 | 3 | T5 | human: 60-minute probe run (A8, A12, A14) with the reader under `systemd-inhibit` (A11) |
| M2 | 1 | T6, T7 | disjoint files |
| M2 | 2 | T8 | edits `sweep.cpp` after T6 |
| M3 | 1 | T9 | |
| M3 | 2 | T10 | same files as T9 |
| M3 | 3 | T11 | same files |
| M4 | 1 | T12 | |
| M4 | 2 | T13 | same files as T12 |
| M4 | 3 | T14 | firmware glue |
| M5 | 1 | T15 | |
| M5 | 2 | T16 | same files as T15 |
| M5 | 3 | T17 | same files |
| M5 | 4 | T18 | same files |
| M6 | 1 | T19 | human: safe first run |
| M6 | 2 | T20, T21, T22 | human, one board, run one after the other |
| M6 | 3 | T23 | human: full run |
| M7 | 1 | T24 | |
| M7 | 2 | T25 | |

## Milestone 1 (M1): Environment validation and harnesses (little to look at: probe firmware, helper harness, two board checks)

- [x] **T1: Add the serial probe firmware and its environment**
  - Covers: none (unblocks T4 and T5, the validation of A8, A9, A11, A12, A14)
  - Depends on: none
  - Files: `platformio.ini` (modify), `tools/probe/serial_probe.cpp` (new)
  - Tests: none (infrastructure)
  - Done when: `pio run -e probe` builds and `pio run` (default environment) still builds the firmware unchanged; `platformio.ini` has `[env:probe]` with the same platform and board as the firmware environment and `build_src_filter = -<*> +<../tools/probe/>`, `default_envs` is still the firmware environment; the probe prints `READY probe reset_reason=<n>` at boot and then one line `P,<seq>,<millis>,<max_loop_gap_us>` per second at 115200 baud via `Serial.println`, and does not touch the PWM pin (plan D12). If the `build_src_filter` form does not compile, use the plan R11 fallback (probe under `src/probe/`, excluded from the firmware environment) and record it in progress.md.
  - Parallel: yes
  - Owner: agent

- [x] **T2: Create the helper harness with `open_port`**
  - Covers: AC15 (port handling part; the board-side check is T4 and T21)
  - Depends on: none
  - Files: `tools/prbs_run.py` (new), `test/helper/test_prbs_run.py` (new), `.gitignore` (modify: add `__pycache__/`)
  - Done when: `python3 -m unittest discover -s test/helper -t test/helper -v` passes, and the new test opens a `pty` slave through `open_port(path) -> int` (F1) and shows with `termios` that the slave is in raw mode at 115200 baud, hardware flow control off and `HUPCL` cleared; the test directory contains no folder named `test_*` (so `pio test -e native` still ignores it) and `pio test -e native` still passes 156 of 156; `open_port` is the only function in the module that calls `termios` (plan D11).
  - Parallel: yes
  - Owner: agent

- [x] **T3: Record the flash and RAM baseline for the one-build decision**
  - Type: env-validation
  - Covers: A10
  - Depends on: none
  - Files: none
  - Tests: none (infrastructure)
  - Done when: `pio run` succeeds on the current tree and the flash and RAM bytes and percentages it prints (22.3 % flash today, about 8.3 % RAM) are recorded in progress.md under Learnings as the baseline; T14 repeats the measurement with the feature (budget: flash below 50 %, plan R12). If the baseline already exceeds the budget: stop and propose the spec change (A10 reopens the one-build decision).
  - Parallel: yes
  - Owner: agent

- [ ] **T4: Validate that opening and closing the port does not reset the board (A9)**
  - Type: env-validation
  - Covers: A9, AC15 [ENV]
  - Depends on: T1, T2
  - Files: none
  - Tests: none (manual)
  - Done when: plan row "A9, AC15" is observed. Steps: (1) flash the probe: `pio run -e probe -t upload`; (2) with a short Python session that imports `tools/prbs_run.py` and calls `open_port("/dev/ttyACM0")`, read the probe lines for 10 s, close the descriptor, and repeat 5 times; (3) start the same script, press Ctrl+C; (4) start it in a terminal window and close the window (SIGHUP). After each step the next received probe line must continue `seq` (no restart to 0) and no new `READY probe` line may appear. If a reset appears only on the very first open: apply the plan D11 fallbacks in order (`stty -F /dev/ttyACM0 -hupcl` before the first open, then `tmux`) and record which one works in progress.md. Evidence for the pass goes to progress.md: for each step the `seq` values before and after and whether a `READY probe` line appeared. If none works: stop and propose the exact spec change (A9/AC15 reopen the spec).
  - Parallel: no
  - Owner: human
  - Blocking: yes

- [ ] **T5: Validate serial pace, reset detection, clock accuracy and no-suspend with the probe (A8, A11, A12, A14)**
  - Type: env-validation
  - Covers: A8, A11, A12, A14, AC11 [ENV]
  - Depends on: T4
  - Files: none
  - Tests: none (manual)
  - Done when: plan rows "A8, AC11", "A11", "A12" and "A14" are observed on one 60-minute probe run. Before starting: record `gsettings get org.gnome.settings-daemon.plugins.power sleep-inactive-ac-type` (it was `suspend`) and start the reader from the beginning under `systemd-inhibit --what=sleep:idle:handle-lid-switch --why="PRBS run" <command>`; `systemd-inhibit --list` must show it for the whole run (if the inhibitor does not hold, record it and use the settings change in the README). Steps: read the probe only with the validated `open_port()` (or `stty -F /dev/ttyACM0 -hupcl` then `cat`), logging each received line with the PC monotonic time; the first 10 minutes with no process holding `/dev/ttyACM0`, then 10 minutes with the reader frozen with `kill -STOP <pid>` and resumed with `kill -CONT <pid>`; press EN once to see `READY probe` and `seq` restarting. Pass if `seq` advances 1 per 1000 ms +/- 5 ms in the lines received after the unattended periods, `max_loop_gap_us` stays below 50 000, and `millis` versus the PC clock over the run stays within 50 ppm (0.18 s per hour). A `seq` restart outside the EN press invalidates the run: repeat it. Evidence goes to progress.md: the `seq` step statistics, the largest `max_loop_gap_us`, the measured ppm and the `systemd-inhibit --list` output. The README task T25 states the inhibitor advice; the 8.75 h USB link itself is proven only by T23 (AC14 handles its failure). If it fails: stop and propose the exact spec change (A8 remedy, A12, or the 2 s tolerance of A14); no spec change for A11 unless the USB link drops.
  - Parallel: no
  - Owner: human
  - Blocking: yes

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 2 (M2): Additions to the existing libraries

- [x] **T6: Add the `prbs` commands to the parser**
  - Covers: AC1, AC3 (command forms)
  - Depends on: none
  - Files: `lib/sweep/include/commands.h`, `lib/sweep/src/commands.cpp`, `lib/sweep/src/sweep.cpp` (only no-op `PrbsLoad`, `PrbsRun` and `BadPrbs` cases in `Sweep::onCommand`), `test/test_commands/test_commands.cpp` (add cases only)
  - Done when: `pio test -e native -f test_commands` passes, and the new cases show: `prbs load 31500` gives `CmdType::PrbsLoad` with `count == 31500`; `prbs load 0` gives `PrbsLoad` with `count == 0`; a count of more than 9 digits gives `count == UINT32_MAX`; `prbs run` gives `PrbsRun`; `prbs`, `prbs load`, `prbs load x`, `prbs foo` give `BadPrbs`; the sweep commands and every existing case still behave as before; `Command` has the third plain member `uint32_t count` without a default initializer and the existing tests' `Command{CmdType::Start, pct}` still compiles (plan D4). `pio run` and `pio test -e native` build without any new compiler warning (in particular no `-Wswitch` warning from `Sweep::onCommand`).
  - Parallel: yes
  - Owner: agent

- [x] **T7: Add `PeriodScheduler::restart`**
  - Covers: AC4 (alignment of second k with measurement period k)
  - Depends on: none
  - Files: `lib/measure/include/measure.h`, `lib/measure/src/measure.cpp`, `test/test_measure/test_measure.cpp` (add cases only)
  - Done when: `pio test -e native -f test_measure` passes, and the new cases show that after `restart(t)` `periodEnded` is false before `t + 1000` and true exactly once at `t + 1000`, `takeSampleNow` is true at `t` and false from `t + 800` to `t + 999`, the 2 ms sample spacing restarts, and the behaviour without `restart` is unchanged (plan D5).
  - Parallel: yes
  - Owner: agent

- [x] **T8: Add `Sweep::observe` and the new ready and unknown-command words**
  - Covers: none (unblocks AC6 and E2; the changed texts are allowed by spec NG8)
  - Depends on: T6
  - Files: `lib/sweep/include/sweep.h`, `lib/sweep/src/sweep.cpp`, `test/test_sweep/test_sweep.cpp` (add cases only)
  - Done when: `pio test -e native -f test_sweep` passes, and the new cases show that `observe(load, ambient)` makes `temp` print the latest values, prints nothing itself, does not latch a cutoff (not even at 80 degrees C or on a rail count) and leaves `dutyCounts()` unchanged; `onPeriod` behaves as before (it now calls `observe` first); the ready commands line is exactly `commands: start [N] (N = 0..<cap> %), stop, results, temp, prbs load N, prbs run` and the unknown-command reply exactly `UNKNOWN COMMAND: valid commands: start [N], stop, results, temp, prbs load N, prbs run`; all 156 earlier tests still pass.
  - Parallel: no
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 3 (M3): The `Prbs` unit

- [x] **T9: Load and validate a sequence in `Prbs`**
  - Covers: AC1, AC2, E5, E7, E8, E11, E12
  - Depends on: none
  - Files: `lib/prbs/include/PrbsConfig.h` (new), `lib/prbs/include/prbs.h` (new), `lib/prbs/src/prbs.cpp` (new), `test/test_prbs/test_prbs.cpp` (new)
  - Done when: `pio test -e native -f test_prbs` passes, and the tests show, with injected time and a recording sink: `beginLoad(N)` prints `PRBS LOAD READY n=<N>`; N valid values then the quiet window (`kLoadTailMs`, no character) give `PRBS LEVELS low=<L> high=<H> changes=<C>` followed by `PRBS LOADED n=<N>` with the right `changes` for a known sequence and `low == high`, `changes=0` for an all-equal one (E5, also with a value 0); each refusal prints exactly the pinned text and clears any earlier sequence (`hasSequence()` false): `REFUSED: bad length <N> (must be 1..32000)` for 0 and 32001 before any value is accepted (E12), `REFUSED: value <i> is not a non-negative integer` for text, a decimal point, a sign, a command line or a blank line inside the stream (E11, a trailing CR ignored), `REFUSED: value <i> = <v> is above the cap <cap>` for `kCapCounts + 1`, `REFUSED: more than two distinct values (value <i>)`, `REFUSED: no data for 5 s (got <k> of <N> values)` after 5000 ms with no value (E7), `REFUSED: more values than announced (<N>)` when a character arrives in the quiet window; a second valid load replaces the first (E8); the bit-packed storage returns the loaded values bit for bit for a 32 000-value pseudo-random two-level sequence (plan D2).
  - Parallel: no
  - Owner: agent

- [x] **T10: Run playback and data lines in `Prbs`**
  - Covers: AC3 (checks 4 to 6 of D8), AC4, AC5, AC7, AC10, AC11 (pace is a function of the clock only), E6
  - Depends on: T9
  - Files: `lib/prbs/include/prbs.h`, `lib/prbs/src/prbs.cpp`, `test/test_prbs/test_prbs.cpp`
  - Done when: `pio test -e native -f test_prbs` passes, and the new tests show: `requestRun` prints exactly `REFUSED: no sequence loaded`, `REFUSED: no measurement yet` or the tolerance refusal (`REFUSED: load must be at ambient temperature first (|load - ambient| = <d> C > 3.0 C)`) in that order of precedence, and starts nothing when refused; an accepted run prints `# RUN START n=<N> cap=<kCapCounts> limit=75.0` then `# columns: k,time_s,u_counts,load_C,ambient_C`; `dutyCounts()` after `tick` equals value k for second k (changing exactly at each `startMs + k*1000`) and every duty is at most `kCapCounts`; one data line `<k>,<time_s>,<u>,<load>,<ambient>` per second with `time_s == k+1` on time and larger when printed late, temperatures with one decimal; after a simulated stall of several seconds every missed line is still printed late with a larger `time_s`, the first one with the fresh period result when it has samples, the others and zero-sample results with the previous line's temperatures, and `nan` when none exists (E6); after the last second the duty is 0, `# RUN END ok n=<N>` is printed after data line N-1 and the state is idle again, ready for a new load; the number of seconds elapsed is derived only from `nowMs`, not from how many times `onPeriod` was called.
  - Parallel: no
  - Owner: agent

- [x] **T11: Safety, stop and latch in `Prbs`**
  - Covers: AC8, AC9, AC10, E3 (the PRBS latch)
  - Depends on: T10
  - Files: `lib/prbs/include/prbs.h`, `lib/prbs/src/prbs.cpp`, `test/test_prbs/test_prbs.cpp`
  - Done when: `pio test -e native -f test_prbs` passes, and the new tests show, for each of load mean count 0 or 4095, ambient mean count 0 or 4095 and load `tempC >= 75.0` (checked in that order, only on results with `samples > 0`): the duty is 0 at once, the line `# RUN END aborted k=<k> reason=<sensor fault load | sensor fault ambient | over-temperature>` is printed with `k` equal to the number of data lines already printed, the data line of that second is not printed, the state is `Latched` and `requestRun` is then refused with `REFUSED: reset required after cutoff`; the check runs before pending catch-up lines are printed after a stall; `stop` during a run sets the duty to 0 at once and prints `# RUN END aborted k=<k> reason=stopped by user` with the state back to idle and no latch; a temperature of 74.9 does not abort; no `u_counts` of any run exceeds `kCapCounts`.
  - Parallel: no
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 4 (M4): Supervisor and firmware glue

- [x] **T12: Add the `Supervisor` routing**
  - Covers: AC3, AC8, AC9, E1, E2, E3, E10
  - Depends on: T6, T8, T11
  - Files: `lib/supervisor/include/supervisor.h` (new), `lib/supervisor/src/supervisor.cpp` (new), `test/test_supervisor/test_supervisor.cpp` (new)
  - Done when: `pio test -e native -f test_supervisor` passes, and the tests show, through `onChar`, `tick`, `onPeriod` and a recording sink: `begin` prints the ready lines; `prbs load`, `prbs run` and `start` obey the precedence of D8 with the pinned texts (`REFUSED: a PRBS run is in progress`, `REFUSED: a duty sweep is in progress`, `REFUSED: reset required after cutoff`; `start` during a sweep keeps `REFUSED: a sweep is already running`); a refusal never changes the stored sequence or the duty (E1, E2, E3, E10, also after a sweep cutoff); while loading every character goes to the load (a `stop` inside the stream refuses the load); `prbs run` accepted makes `takeMeasurementRestart()` return true exactly once; a `stop` sent during a run reaches `Prbs::stop`: the duty is 0 at once and `# RUN END aborted k=<k> reason=stopped by user` is printed (AC9); after a PRBS abort (latch) `start` is refused with `REFUSED: reset required after cutoff`, as are `prbs run` and `prbs load`, until a new `Supervisor` (reset) (AC8, E3); `dutyCounts()` is the run's duty while a run is in progress and the sweep's otherwise; during a run `Supervisor` feeds `Sweep::observe` and `Prbs::onPeriod`, never `Sweep::onPeriod`; the sweep still works normally outside a run (a short simulated sweep).
  - Parallel: no
  - Owner: agent

- [x] **T13: Print replies as `#` lines during a run**
  - Covers: AC6, E2 (`temp` answered)
  - Depends on: T12
  - Files: `lib/supervisor/include/supervisor.h`, `lib/supervisor/src/supervisor.cpp`, `test/test_supervisor/test_supervisor.cpp`
  - Done when: `pio test -e native -f test_supervisor` passes, and the new tests show a whole simulated run in which every line printed from `# RUN START` to `# RUN END` is either a data line or starts with `#`; a `temp` sent mid-run is answered as `# TEMP load=...`, a refused `start` or `prbs load` as `# REFUSED: a PRBS run is in progress`, `results` and an unknown command also as `#` lines; the sweep's periodic `LOG` line never appears in a run; the first line printed is `# RUN START ...` and the last `# RUN END ...`; a reply longer than the 160-byte prefix buffer is truncated, not overflowed (plan D6).
  - Parallel: no
  - Owner: agent

- [x] **T14: Wire `main.cpp` to the `Supervisor`**
  - Covers: none (unblocks the hardware checks of AC4, AC7, AC10; timing is verified in T20)
  - Depends on: T12, T13 (T7 from M2)
  - Files: `src/main.cpp`
  - Tests: none (hardware-glue): `main.cpp` needs `Arduino.h`, the pins and `millis()`; the logic it forwards is tested in T7 and T12-T13
  - Done when: `pio run` and `pio test -e native` pass; `main.cpp` calls `Serial.setRxBufferSize(1024)` before `Serial.begin(115200)`, forwards each received character to `Supervisor::onChar`, calls `tick` every pass, calls `Supervisor::onPeriod` once per measurement period, and when `takeMeasurementRestart()` is true calls `PeriodScheduler::restart(now)` and `reset()` on both averagers; `writeDutyCounts()` (through `clampDuty`) is still the only caller of `ledcWrite`; the flash and RAM percentages of `pio run` are recorded in progress.md and flash is below 50 % (A10, plan R12; if not, stop and propose the spec change).
  - Parallel: no
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 5 (M5): The helper program

- [x] **T15: Read and validate the sequence file; log and exit-code skeleton**
  - Covers: AC12 (sequence file part), E9, E12 (helper side)
  - Depends on: T2
  - Files: `tools/prbs_run.py`, `test/helper/test_prbs_run.py`, `test/helper/fake_board.py` (new)
  - Done when: `python3 -m unittest discover -s test/helper -t test/helper -v` passes, and the tests show, through `main(argv, timeouts)` with a `pty` fake board that records the bytes it receives: both file forms of A7 are read (plain list; CSV with the header `Tempo_s,PWM_counts` and `k` = 0, 1, 2, ... without gaps, LF and CRLF, trailing blank lines ignored, a copy of the shape of `docs/sinal_prbs.csv` giving N = 31 500, low 1266, high 1490, changes 158); a file with 0 values, more than 32 000 values, a non-integer, a wrong header, a gap in `k`, or an unreadable path exits with status 2 and the fake board received no byte (E12); a log path that cannot be written exits with status 2 before the port is opened (E9); the log file is created or appended to.
  - Parallel: no
  - Owner: agent

- [x] **T16: Handshake, load and run start in the helper**
  - Covers: AC12 (load and confirm), AC13, E12 (board refuses the announcement)
  - Depends on: T15
  - Files: `tools/prbs_run.py`, `test/helper/test_prbs_run.py`, `test/helper/fake_board.py`
  - Done when: the helper tests pass, and with a scripted fake board they show: the helper synchronises by sending `temp` until a line starting `TEMP` or `# TEMP` arrives (up to 5 tries, spacing `Timeouts.sync`); sends `prbs load N`, waits for `PRBS LOAD READY n=N` and only then sends the N value lines in writes of at most 256 bytes, the last one terminated; accepts `PRBS LEVELS low=<L> high=<H> changes=<C>` and `PRBS LOADED n=N` when equal to the values computed from the file, shows both lines on the console (the test captures the helper's standard output) and then sends `prbs run`; exits with status 2 and sends no `prbs run`, printing the board's reason, for: a `REFUSED:` line (also when prefixed with `# `), a different count, different `low`, `high` or `changes`, no `PRBS LOAD READY` within `Timeouts.ready`, no confirmation within `Timeouts.loaded` of the last value; after `prbs run` it exits with status 2 and sends `stop` when no `# RUN START` arrives within `Timeouts.run_start` or a `REFUSED:` arrives; when the board refuses the announcement the fake board received no value line (E12).
  - Parallel: no
  - Owner: agent

- [x] **T17: Log the run and set the exit status in the helper**
  - Covers: AC12 (log and exit status)
  - Depends on: T16
  - Files: `tools/prbs_run.py`, `test/helper/test_prbs_run.py`, `test/helper/fake_board.py`
  - Done when: the helper tests pass, and they show: from `# RUN START` on every received line is appended to the log and flushed (and `fsync`ed) before the next read, while earlier lines are only shown on the console; `# RUN END ok n=<N>` ends with status 0 and `# RUN END aborted ...` with status 1; a line that is neither a `#` comment nor `<int>,<int>,<int>,<number or nan>,<number or nan>` is written as `# CORRUPT <line>` and counted on the console at the end.
  - Parallel: no
  - Owner: agent

- [x] **T18: End an incomplete log correctly in the helper**
  - Covers: AC14, AC15 (helper side), E4 (helper side)
  - Depends on: T17
  - Files: `tools/prbs_run.py`, `test/helper/test_prbs_run.py`, `test/helper/fake_board.py`
  - Done when: the helper tests pass, and they show that with the fake board closing the pty, no line for `Timeouts.silence`, the line `READY` in the stream, or a `SIGINT`, `SIGTERM` or `SIGHUP` sent to the helper process, the log keeps its lines, ends with exactly one of `# LOG INCOMPLETE serial error: <text>`, `# LOG INCOMPLETE no line received for 10 s`, `# LOG INCOMPLETE board reset (READY seen)`, `# LOG INCOMPLETE interrupted by <signal>`, contains no `# RUN END`, and the exit status is 3; a pause of the helper longer than `Timeouts.silence` followed by buffered lines does not count as a silent board.
  - Parallel: no
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 6 (M6): Hardware acceptance (human, one board)

- [ ] **T19: Safe first run on the board (M1 procedure)**
  - Covers: AC1, AC5, AC10, AC12 (first real run)
  - Depends on: T14, T18
  - Files: none
  - Tests: none (manual)
  - Done when: plan procedure "M1: AC1, AC5, AC10, AC12 short first run" gives its expected observations. Prerequisites: the product firmware is flashed from the 50 % cap build (`pio run -t upload`; `READY` shows `N = 0..50` and the `prbs` commands), the board is powered from USB, the load starts within 3.0 degrees C of ambient, the user is in the `dialout` group, Python 3 is installed, the helper runs inside `tmux`, and a multimeter is on D14. First output: `PRBS LEVELS low=400 high=600 changes=5`, `PRBS LOADED n=300`, `# RUN START n=300 cap=2047 limit=75.0`, then one data line per second ending `# RUN END ok n=300`. If anything is unexpected stop and press EN or send `stop`.
  - Parallel: no
  - Owner: human
  - Blocking: yes

- [ ] **T20: Timing, end and stop on the board (M2 procedure)**
  - Covers: AC4, AC7, AC9
  - Depends on: T19
  - Files: none
  - Tests: none (manual)
  - Done when: plan procedure "M2: AC4, AC7, AC9 timing, end and stop" gives its expected observations (one line per second with `time_s = k+1`, duty 0 and an idle board after `# RUN END ok`, `stop` ending the run within 1 s with exit status 1).
  - Parallel: no
  - Owner: human
  - Blocking: no

- [ ] **T21: Pause, interruption, reset and unplug behaviours (M3 procedure)**
  - Covers: AC11 [ENV], AC14 [ENV], AC15 [ENV], E4
  - Depends on: T19, T5
  - Files: none
  - Tests: none (manual)
  - Done when: plan procedure "M3: AC11, AC14, AC15, E4 environment behaviours (N = 1800, levels 400 and 600)" gives its expected observations in all six steps (10-minute pause with a `k` gap, Ctrl+C and terminal close without a board reset, EN press giving `# LOG INCOMPLETE board reset (READY seen)`, USB unplug giving a serial-error or silence marker, `# CORRUPT` lines still readable by `readmatrix`). If A8 or AC15 fails here: stop and propose the exact spec change.
  - Parallel: no
  - Owner: human
  - Blocking: no

- [ ] **T22: Sensor fault, refusals and tolerance on the board (M4 procedure)**
  - Covers: AC3, AC8, AC13 [ENV], E10
  - Depends on: T19
  - Files: none
  - Tests: none (manual)
  - Done when: plan procedure "M4: AC8 sensor fault (hardware), AC3, AC13 refusals" gives its expected observations in all four steps (shorted load sensor aborts with `reason=sensor fault load` and later runs are refused until reset; a value of 3000 refused with the cap; `start` sweep in progress refuses the helper and the sweep continues; a warm load refuses with the tolerance text).
  - Parallel: no
  - Owner: human
  - Blocking: no

- [ ] **T23: Full acceptance run with `docs/sinal_prbs.csv` (M5 procedure)**
  - Covers: AC7, AC11, AC12 (full length)
  - Depends on: T20, T21, T22
  - Files: none
  - Tests: none (manual)
  - Done when: plan procedure "M5: full acceptance run" gives its expected observations: `PRBS LEVELS low=1266 high=1490 changes=158`, `PRBS LOADED n=31500`, 31 500 data lines, `# RUN END ok n=31500` after 31 500 s (+/- 2 s), helper exit status 0, and `readmatrix('<log>','CommentStyle','#')` returning a 31 500 x 5 matrix. Prerequisite: the helper under `tmux` and `systemd-inhibit`, the board powered from USB, T19 to T22 passed.
  - Parallel: no
  - Owner: human
  - Blocking: no

> MILESTONE CHECKPOINT: stop and report to the human.

## Milestone 7 (M7): Validation and documentation sync

- [x] **T24: Run full verification**
  - Covers: every AC/E (verification only)
  - Depends on: every agent task (T1-T3, T6-T18)
  - Files: none
  - Tests: none (infrastructure)
  - Done when: `pio run && pio test -e native && python3 -m unittest discover -s test/helper -t test/helper` passes, and every AC/E in the matrix below has a passing check or a documented manual check; every `deferred` row is listed under "Deferred / unverified".
  - Parallel: no
  - Owner: agent

- [ ] **T25: Sync documentation**
  - Covers: plan.md "Documentation impact" rows marked `task` (`README.md`; `.gitignore` was done in T2)
  - Depends on: T24, T4, T5
  - Files: `README.md`
  - Tests: none (docs-only)
  - Done when: `README.md` contains the new section from the plan: accepted sequence files (plain list and the `Tempo_s,PWM_counts` CSV), MATLAB export and `readmatrix(file,'CommentStyle','#')`, how to run the helper with `--port`, `--seq`, `--log`, the exit codes 0/1/2/3, the log format including `# RUN START`, `# columns`, `# RUN END`, `# LOG INCOMPLETE` and `# CORRUPT`, the 75 degrees C limit, the resistor-power caveat (0.30 W and 0.35 W per resistor against 1/4 W), Linux only and Python 3, the `dialout` group, `tmux` and `systemd-inhibit` advice, the reset-on-open caveat and the fallbacks recorded in progress.md by T4 and the inhibitor result by T5, that the last sequence line must end in a newline and that a client must wait for `PRBS LOADED` before `prbs run`, the `prbs load N` and `prbs run` rows in the command table, the new files in the layout list, and the probe environment; the diff touches only `README.md`.
  - Parallel: no
  - Owner: agent

> MILESTONE CHECKPOINT: stop and report to the human.

## Deferred / unverified

Human rows that may still be pending (`Blocking: no`) when T24 runs. The feature is not "done" until each is verified.

- T20 (timing, end, stop on the board) -> AC4, AC7, AC9, procedure M2
- T21 (pause, interruption, reset, unplug) -> AC11, AC14, AC15, E4, procedure M3
- T22 (sensor fault, refusals, tolerance) -> AC3, AC8, AC13, E10, procedure M4
- T23 (full acceptance run) -> AC7, AC11, AC12, procedure M5

## Out of tasks (needs approval)

- `CLAUDE.md` (SDD section) -> `Test: pio test -e native` and `python3 -m unittest discover -s test/helper -t test/helper`; `Verify (all)` as in the header above (instruction file)
- `specs/001-duty-sweep/spec.md` and `plan.md` -> still say the 25 % cap; spec 002 supersedes it for this firmware (the owner's call)
- `docs/Prj1CDig.pdf` -> the plan row about the broken symlink no longer applies: `docs/Prj1CDig.pdf` is now a regular file; nothing to do

## Coverage matrix

| ID | Description (short) | Tasks | Verified by |
| --- | --- | --- | --- |
| AC1 | load N values, `PRBS LEVELS`, `PRBS LOADED` | T6, T9, T12, T16, T19 | auto + manual |
| AC2 | all load refusals clear the sequence | T9 | auto |
| AC3 | run refused for no sequence, running, latch, tolerance | T6, T10, T12, T22 | auto + deferred |
| AC4 | second k plays value k, one line within 0.1 s | T7, T10, T20 | auto + deferred |
| AC5 | data line format, columns comment | T10, T19 | auto + manual |
| AC6 | START/END framing, only `#` and data inside | T10, T11, T13 | auto |
| AC7 | end: duty 0, `RUN END ok`, idle | T10, T20, T23 | auto + deferred |
| AC8 | 75 degrees C and sensor faults abort and latch | T11, T12, T22 | auto + deferred |
| AC9 | `stop` ends the run | T11, T12, T20 | auto + deferred |
| AC10 | duty never above the cap | T10, T11, T19 | auto + manual |
| AC11 [ENV] | pace unaffected by a paused reader | T5, T10, T21, T23 | manual + deferred |
| AC12 [ENV] | helper sends, confirms, runs, appends, exit codes | T15, T16, T17, T19, T23 | auto + manual |
| AC13 [ENV] | helper stops on refusal, mismatch, timeouts | T16, T22 | auto + deferred |
| AC14 [ENV] | serial loss or reset -> `# LOG INCOMPLETE` | T18, T21 | auto + deferred |
| AC15 [ENV] | stopping the helper does not reset the board | T2, T4, T18, T21 | manual + deferred |
| E1 | load during a run refused | T12 | auto |
| E2 | run or `start` during a run refused, `temp` answered | T12, T13 | auto |
| E3 | run after cutoff latch refused | T11, T12 | auto |
| E4 | reset during a run | T18, T21 | auto + deferred |
| E5 | all-equal or zero values accepted | T9 | auto |
| E6 | stalled period: line printed with held values or `nan` | T10 | auto |
| E7 | load interrupted: refused after 5 s | T9 | auto |
| E8 | new load replaces the old sequence | T9 | auto |
| E9 | unwritable log: nothing sent, non-zero exit | T15 | auto |
| E10 | load or run during a sweep or after its latch refused | T12, T22 | auto + deferred |
| E11 | non-integer line inside a load refuses it | T9 | auto |
| E12 | 0 or more than 32 000 values refused, helper sends no values | T9, T15, T16 | auto |
| A8, A9, A12, A14 | serial pace, port open/close, READY on reset, clock | T4, T5 | manual |
| A10 | one build fits | T3, T14 | auto |
| A11 | no suspend | T5 | manual |
