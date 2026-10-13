# Progress

Feature: Open-loop duty sweep for the thermal load

## Completed
- T1 — added `[platformio] default_envs`, pinned `espressif32@7.1.3`, monitor DTR/RTS off, `[env:native]` (Unity 2.6.1 fetched on first run), and `test/test_smoke`; `pio test -e native` and `pio run` pass. Reviewer PASS.
- T2 — `lib/sweep`: `SweepConfig.h` (D12 constants, `sweepcfg::` namespace, names chosen by the implementer because the plan gave only values), `duty.h/.cpp` (`clampDuty`, `percentToCounts`, `countsToPercent`); 11 tests, reviewer PASS.

## Decision
(Decisions that did not follow the specification, and the reason for the change.)
- Run base: `Base: HEAD e59a5b9` on branch `feat/duty-sweep` (created from master; working tree clean; untracked PILOT-LOG.md excluded locally via .git/info/exclude).
- Override (2026-10-13): tasks.md is Draft; the user explicitly ordered the whole file to be executed, to commit when pertinent and not to stop at milestones unless blocked.
- T1..T9, T12, T13 are agent tasks and run in this order sequentially (no parallel worktrees: wave 2 is cheap and worktrees add cost). T10/T11 need the physical board and cannot be done by the agent: they stay pending, and T12/T13 run without waiting for them (dependency waived by the user's instruction to finish).

## Blocked
- (none) Format: Blocked: T# — <what was tried, observed failure, question for the human>

## Follow-ups
(Out-of-scope ideas; do not act on them inside this feature.)
- plan.md lists a 'step list' under duty.h; T2's Done when did not require it, so it is built where T6 needs it.

## Learnings
(Gotchas future tasks should know.)
- `pio` is not on PATH; `export PATH="$HOME/.platformio/penv/bin:$PATH"` before `pio ...`.
- Unity is downloaded by `pio test -e native` on first use (network needed once); it worked here.
