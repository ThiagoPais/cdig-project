# Progress

Feature: Open-loop duty sweep for the thermal load

## Completed
- T1 — added `[platformio] default_envs`, pinned `espressif32@7.1.3`, monitor DTR/RTS off, `[env:native]` (Unity 2.6.1 fetched on first run), and `test/test_smoke`; `pio test -e native` and `pio run` pass. Reviewer PASS.
- T2 — `lib/sweep`: `SweepConfig.h` (D12 constants, `sweepcfg::` namespace, names chosen by the implementer because the plan gave only values), `duty.h/.cpp` (`clampDuty`, `percentToCounts`, `countsToPercent`); 11 tests, reviewer PASS.
- T3 — `lib/measure`: `countToCelsius`, `WindowAverager` (reset() holds the last mean; empty period returns samples = 0), `PeriodScheduler` (origin = first call, 2 ms spacing in the first 800 ms of each 1000 ms, one `periodEnded` per period); 14 tests, reviewer PASS.
- T4 — `lib/sweep` `commands.h/.cpp`: `LineBuffer::push` parses `start [N]`, `stop`, `results`, `temp`; 32-byte buffer, overlong line -> Unknown, empty line ignored; 21 tests, reviewer PASS (2 minors accepted, see Follow-ups).
- T5 — `lib/sweep` `sweep.h/.cpp`: `Sweep` shell (state machine Idle/HoldZero/Running/Done/Cutoff, ready message, start/stop/temp handling, cutoff on uncorrected load >= 80 or rail counts, duty 0 latched); 34 tests, reviewer PASS. Output formats were not defined by plan/tasks, so the executor fixed them in the test-writer brief (READY, START from N %, REFUSED: ..., STOPPED, NOTHING RUNNING, CUTOFF ..., TEMP load=.. count=.. n=.. ambient=..); see Decision.
- T6 — `Sweep`: 0 % hold, steps `N + k*2.5` (<= 25, last not forced), settle = max-min over the last 120 per-second entries <= 0.5, 45 min timeout, offset = mean(load - ambient) over the last window, rows (`rowCount/row/offsetKnown/offsetC`), cutoff on uncorrected values; history ring tied to `sweepcfg::kSettleBufferSize`. Review round 1 FAIL (major): zero-sample periods fed the settle history/offset, contrary to plan D4; test-writer added 6 tests (4 failed first), fix applied, round 2 PASS. 111 tests total.
- T7 — `Sweep::printTable/interpolateDuty`: `TABLE offset=..`, column line, one row per step (duty % = counts/4095), `TARGET 55/60/65` lines (first bracketing pair of settled rows, ends included, lower duty on equal loads), printed after `SWEEP DONE` and on `results` (targets only in Done); `kTargetsC` added to SweepConfig.h. One test expectation corrected by the test-writer (`2.50` -> `2.49`: 102 counts is 2.49 %, spec E8); reviewer PASS, correction judged legitimate. 130 tests total.
- T8 — `Sweep::maybeLog`: `LOG t=<s> duty=<pct>% (<counts>) load=.. C ambient=.. C load_adc=.. ambient_adc=..` printed in the period that first reaches a multiple of 5 s since `start`, only while HoldZero/Running, load corrected once the offset is known; 17 tests incl. cap on every period for `start`, `start 10`, `start 25` and an end-to-end simulated sweep within 0.5 %. A truncated expectation (`10` instead of `102` counts) was corrected by the test-writer. Reviewer PASS. 147 tests total.
- T9 — `src/main.cpp`: D14 LOW first, 115200 baud, 12-bit ADC at 0 dB on GPIO33/GPIO34, LEDC ch0 1 kHz 12-bit on GPIO14 through the single `writeDutyCounts()` (clampDuty), cooperative sampling/period flow into `Sweep`; `pio run` builds (RAM 8.3 %, flash 22.3 %), 147 host tests pass. Reviewer PASS; its minor (sample taken before the period end) was fixed by reordering `periodEnded` before `takeSampleNow`. Not run on hardware: see T10/T11.
- T12 (automated part, NOT ticked) — `pio run && pio test -e native` pass (147 tests, firmware builds); final-mode spec-reviewer PASS over `e59a5b9..HEAD` with the AC1-AC17 / E1-E14 matrix: covered on host, hardware-only items pending in T10/T11 (AC1 timing, AC9 shorted input, AC10 at the pin, AC14 sample count, AC17, E9, E10, E12 buffer). T12 stays unticked until those observations exist.
- T13 — `README.md` (pins, commands, constants and where each limit lives, duration, safety, 3.3 V caveat). Review needed 3 fix rounds, all factual accuracy (limits location, example numbers, row-count bound); PASS in round 3.

## Decision
(Decisions that did not follow the specification, and the reason for the change.)
- Run base: `Base: HEAD e59a5b9` on branch `feat/duty-sweep` (created from master; working tree clean; untracked PILOT-LOG.md excluded locally via .git/info/exclude).
- Override (2026-10-13): tasks.md is Draft; the user explicitly ordered the whole file to be executed, to commit when pertinent and not to stop at milestones unless blocked.
- T1..T9, T12, T13 are agent tasks and run in this order sequentially (no parallel worktrees: wave 2 is cheap and worktrees add cost). T10/T11 need the physical board and cannot be done by the agent: they stay pending, and T12/T13 run without waiting for them (dependency waived by the user's instruction to finish).

- Output text formats: plan.md says 'fixed formats chosen in the tasks' but tasks.md did not choose them. The executor chose them while briefing the T5 test-writer (substrings asserted by tests/test_sweep): `READY`, `START from <N> %`, `REFUSED: ...` (reasons: reset required, already running, no measurement, ambient, usage), `STOPPED`, `NOTHING RUNNING`, `CUTOFF load ...`, `CUTOFF <sensor> sensor reading invalid`, `UNKNOWN COMMAND`, `TEMP load=<t> C count=<c> n=<n> ambient=...`. Spec is silent on wording, so no deviation from it.

## Blocked
- (none) Format: Blocked: T# — <what was tried, observed failure, question for the human>

## Follow-ups
(Out-of-scope ideas; do not act on them inside this feature.)
- T10 and T11 (human, hardware): flash and run the manual checks; then tick T12.
- `Sweep::addRow` drops rows silently beyond `kMaxRows` (12); a step size below ~2.1 % would lose rows. Consider a static_assert tying kMaxRows to kCapPercent/kStepPercent.
- `haveMeasurement_` is set by a period with zero samples; a `start` just after boot can be refused with 'no measurement yet' instead of queued (reviewer minors on the final pass).
- `kStartupBoundS` is defined but unused; `kMaxStartPercent` (25) in commands.cpp duplicates `kCapPercent`.
- `StepRow::dutyPercent` (nominal) vs printed percentage (counts/4095).
- plan.md lists a 'step list' under duty.h; T2's Done when did not require it, so it is built where T6 needs it.
- Parser accepts `start 0x10`, `start +5`, `start 1e1` (strtof forms) and treats a line of 32+ characters as Unknown even if the tail is padding; both are harmless because N is bounded 0..25 and the cap holds downstream. Tighten only if wanted.
- Log lines on a zero-sample period (stalled loop) show the held values without marking them stale (reviewer minor, consistent with plan D4). No test starts near the uint32 `millis()` wrap (arithmetic is wrap-safe by inspection).
- `StepRow::dutyPercent` holds the nominal step (25.0) while `dutyCounts` is 1023 (24.98 %). T6 tests pin the nominal value; the printed table (T7) shows `countsToPercent(dutyCounts)` as E8 requires.

## Learnings
(Gotchas future tasks should know.)
- README claims about 'where each limit lives' were wrong twice: the duty cap is `kCapCounts` in duty.h, not SweepConfig.h; always check docs against the code.
- Do not run `pio test -e native` while another task's failing tests exist in the tree: a missing header aborts the whole run. Reviews therefore run strictly after the implementation and before the next task's test-writer.
- `pio` is not on PATH; `export PATH="$HOME/.platformio/penv/bin:$PATH"` before `pio ...`.
- Unity is downloaded by `pio test -e native` on first use (network needed once); it worked here.
