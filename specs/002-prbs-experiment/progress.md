# Progress

Feature: Open-loop PRBS experiment and data log

## Completed
- T1 — `[env:probe]` in `platformio.ini` (same platform/board, `build_src_filter = -<*> +<../tools/probe/>`, so plan R11 fallback not needed) and `tools/probe/serial_probe.cpp` (prints `READY probe reset_reason=<n>`, then `P,<seq>,<millis>,<max_loop_gap_us>` per second with `Serial.printf`; no PWM). `pio run -e probe` and `pio run` build. Reviewer PASS (one minor: `printf` instead of the literal `println`, same serial path).
- T2 — `tools/prbs_run.py` with `open_port(path) -> int` (raw 115200 8N1, no flow control, `HUPCL` and `CRTSCTS` cleared, `CLOCAL|CREAD`, never touches DTR/RTS; only function using `termios`); `test/helper/test_prbs_run.py` (9 tests via a pty); `.gitignore` gets `__pycache__/`. 9 helper tests and the 156 native tests pass. Reviewer PASS.
- T6 — `commands.h/.cpp`: `CmdType::PrbsLoad/PrbsRun/BadPrbs`, `Command::count` (plain member, set to 0 by `make()`), parser for `prbs load <N>` (digits only, >9 digits give UINT32_MAX) and `prbs run`; `sweep.cpp` got no-op cases for the three new values (no `-Wswitch`); 12 new tests, 168 tests pass, no warnings. Reviewer PASS.
- T7 — `PeriodScheduler::restart(nowMs)` (origin = now, period counter 0, sample spacing reset; no change without it); 7 new tests (AC4 alignment), 175 tests pass. Reviewer PASS.
- T8 — `Sweep::observe(load, ambient)` (records values for `temp`, no output, no cutoff, no state/duty change; `onPeriod` calls it first); ready commands line and `UNKNOWN COMMAND` reply now list `prbs load N, prbs run`; buffer in `begin` 96 -> 128; 12 new tests, 187 tests pass, no warnings. Reviewer PASS.
- Milestone M2 checkpoint (autonomous, no stop): T6-T8 done, all reviewers PASS first round; `pio test -e native` 187/187, `pio run` OK (flash 22.3 %, RAM 8.3 %).
- T9 — new `lib/prbs`: `PrbsConfig.h` (`prbscfg::kMaxValues/kLoadTimeoutMs/kLoadTailMs/kLineMax/kLimitC`), `prbs.h`/`prbs.cpp` with the load half of `Prbs` (bit-packed 4000-byte storage + two levels, `beginLoad`, `onLoadChar`, `tick`, quiet window, all pinned REFUSED/LEVELS/LOADED texts); 44 tests, 231 total pass, no warnings. Reviewer PASS. Note for T10: the 32 000-value bit-for-bit check can only be done through the run duty (no accessor), so T10's playback test must compare against a 32 000-value reference.
- T10 — `Prbs::requestRun` (no sequence / no measurement / 3.0 C tolerance refusals in that order), `tick` (duty = value at `(now-start)/1000`, 0 after the last second), `onPeriod` (data lines for every completed unprinted second with catch-up and held/`nan` temperatures, `# RUN END ok n=<N>`), `running()`, `dutyCounts()`; the 32 000-value bit-for-bit playback test from T9 is in. First test round: 3 of 29 new tests were wrong (two loaded 3 distinct values, one counted 5 lines instead of 6); fixed by test-writer with a written diagnosis (`Test corrections` passed to the reviewer). 259 tests pass, no warnings. Reviewer PASS.
- T11 — `Prbs::stop`, `latched()`, safety check at the start of `onPeriod` (load rail, ambient rail, load >= 75.0 C; only results with samples; `k` = data lines already printed; before catch-up lines), `REFUSED: reset required after cutoff` while latched; 31 new tests, 290 tests pass, no warnings. Reviewer PASS.
- Milestone M3 checkpoint (autonomous, no stop): T9-T11 done; one test-correction round in T10; `pio test -e native` 290/290, `pio run` OK. Note: `lib/prbs` is not linked into the firmware until T14.
- T12 — new `lib/supervisor` (`Supervisor`: char routing to the load or the `LineBuffer`, D8 precedence for `prbs load`/`prbs run`/`start` with the Sweep keeping its own texts, shared latch, one-shot `takeMeasurementRestart`, `stop` to `Prbs::stop`, `BadPrbs` answered as unknown command, `Sweep::observe` + `Prbs::onPeriod` during a run, duty ownership); 44 tests, 334 total pass, no warnings. One test-correction round (helper `runBusyCase` used a 5.0 C load/ambient difference then expected a new run accepted; AC3 refuses it). Reviewer PASS.
- T13 — `PrefixSink` in `lib/supervisor`: replies printed during a run get the prefix `# ` (set per command from `prbs_.running()`, cleared for the Sweep's period handler outside a run), run lines go through the raw sink; 160-byte buffer truncates with `snprintf` (not unit-testable through the pinned interface; judged by the reviewer from the code). 13 new tests, 347 pass, no warnings. Reviewer PASS.
- T14 — `src/main.cpp` wired to `Supervisor`: `Serial.setRxBufferSize(1024)` before `begin`, `onChar` per character, `tick` every pass, `takeMeasurementRestart()` then `scheduler.restart(now)` + both averagers `reset()` before `periodEnded`, `supervisor.onPeriod`, `supervisor.dutyCounts()` through the unchanged `writeDutyCounts()`. `pio run` OK: flash 296949 B = 22.7 % (budget < 50 %, A10 holds), RAM 31472 B = 9.6 % (baseline 8.3 %). Not run on hardware (T19). Reviewer PASS.
- Milestone M4 checkpoint (autonomous, no stop): T12-T14 done; one test-correction round in T12; `pio test -e native` 347/347, helper tests 9/9, `pio run` OK.
- T3 — baseline recorded (no code change): flash 292317 bytes = 22.3 % of 1310720, RAM 27328 bytes = 8.3 % of 327680. Budget for A10: flash below 50 %. No review run: the task changes no file.

## Decision
(Decisions that did not follow the specification, and the reason for the change.)
- Run base (2026-10-19): `Base: HEAD 5255a18` on branch `feat/prbs-experiment`; working tree clean after the documentation commit `5255a18` (spec/plan/tasks/progress, docs/sinal_prbs.csv, docs/logs, docs/Prj1CDig.pdf), chosen by the user.
- User instructions (2026-10-19): (1) run all agent tasks first and leave the human tasks to the end; (2) commit only at the end of each milestone; (3) no milestone stops (stop only on Blocked, hard stops). Order override: T4 and T5 (`Blocking: yes`, M1) are NOT run before the code tasks; the risk is that A8/A9 stay unvalidated while M2-M5 are built (a failure reopens the spec and may cost rework). T25 depends on T4/T5 and therefore stays pending until they are done. Sequential execution (no parallel worktrees).

- Decision (T12, orchestrator): the plan pins no reply for a `BadPrbs` line (e.g. `prbs foo`); the `Supervisor` answers it like an unknown command, with the pinned `UNKNOWN COMMAND: valid commands: ... prbs load N, prbs run` text, instead of inventing a new text.

## Blocked
- Deferred: T4 - (human) flash the probe (`pio run -e probe -t upload`) and validate that opening/closing the port with `open_port()` does not reset the board (procedure in tasks.md T4).
- Deferred: T5 - (human) 60-minute probe run with `systemd-inhibit` (A8, A11, A12, A14).
- Deferred: T19-T23 - (human) hardware acceptance procedures M1-M5 of the plan.
- (none) Format: Blocked: T# — <what was tried, observed failure, question for the human>
- Formats: `Deferred: T# - <what the human must do>`, `Resolved: T# - <how>`. The unattended Blocked regex only matches `Blocked: T#`.

## Follow-ups
(Out-of-scope ideas; do not act on them inside this feature.)
- After a load refusal in the middle of the stream (e.g. a value above the cap at value 5), the `Supervisor` goes back to command mode and every remaining value line the helper keeps sending is answered with an `UNKNOWN COMMAND ...` line (about 80 bytes per input line). The plan does not cover it. Mitigation idea for T16: the helper checks for an incoming `REFUSED:` line between its 256-byte writes and stops sending; a board-side drain mode would be a plan change.

## Learnings
(Gotchas future tasks should know.)
- `/usr/bin/pio` is broken on this machine (click `resultcallback` AttributeError). Always `export PATH="$HOME/.platformio/penv/bin:$PATH"` before `pio ...`; plan/tasks wrongly say `pio` is on PATH.
- A8-A14 baseline for A10 (T3): flash 292317 B / 22.3 %, RAM 27328 B / 8.3 % before the feature; probe env: flash 20.4 %, RAM 6.6 %.
