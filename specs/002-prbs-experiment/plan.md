Status: Approved (Thiago Cardoso, 2026-10-19)

# Plan: Open-loop PRBS experiment and data log

Spec: `specs/002-prbs-experiment/spec.md` (Status: Approved (Thiago Cardoso, 2026-10-19))

## Approach summary
The firmware gains a hardware-free `Prbs` unit (load protocol, bit-packed two-level storage, the 1 s playback, the 75 degrees C safety, the `#`/data log lines) and a small `Supervisor` that owns the serial console and decides who may drive the heater (sweep or PRBS run). Both are host-tested with injected time, injected measurements and a recording `LineSink`, like feature 001. The run reuses the existing 0.8 s window measurement: at `prbs run` the period scheduler is restarted so that second k of the run is exactly measurement period k, and the duty of each second is computed from the board clock, never from counting events, so a late loop catches up instead of drifting. A stdlib-only Python helper (`tools/prbs_run.py`, Linux) sends the file, checks the confirmation, starts the run, and appends every line from `# RUN START` to the log, ending the log with `# RUN END ...` (from the board) or `# LOG INCOMPLETE <reason>` (from the helper). Because several `[ENV]` criteria depend on how this board and PC behave (serial blocking, DTR reset on open/close, clock accuracy, suspend), a small probe firmware and an early validation step come before the real implementation.

## Sources consulted

| Path / reference | Read | Why it matters |
|------------------|------|----------------|
| `specs/002-prbs-experiment/spec.md` | full | the contract: G1-G4, NG1-NG9, AC1-AC15, E1-E12, A1-A14, constraints |
| `CLAUDE.md` | full | SDD section: verify `pio run`, test patterns `test/**`, never weaken tests, never edit spec/plan unapproved, commit convention none established |
| `specs/README.md` | full | feature layout, Status rule |
| `specs/001-duty-sweep/spec.md` | headings + AC1, AC14 | READY line, window measurement definition |
| `specs/001-duty-sweep/plan.md` | full | decisions D1-D13 to stay consistent with (injected time/sink, `writeDutyCounts` choke point, one cap, formats) and its risk list (R5, R11) |
| `specs/001-duty-sweep/tasks.md` | headings + T1 | verify commands, commit convention text, `-f <folder>` filter |
| `specs/001-duty-sweep/progress.md` | full | 50 % cap accepted 2026-10-18; closed terminal resets the board; learnings (README accuracy, `pio` PATH) |
| `README.md` | full | current commands, safety text, cap section; what must be extended |
| `platformio.ini` | full | `espressif32@7.1.3`, `monitor_dtr/rts = 0`, `-DSWEEP_CAP_PERCENT=50`, `[env:native]` with `build_src_filter = -<*>` |
| `src/main.cpp` | full | glue to replace: loop order, `writeDutyCounts`, scheduler/averagers |
| `lib/sweep/include/*.h`, `lib/sweep/src/*.cpp` | full | `LineSink`, `Sweep`, `LineBuffer` (32-byte buffer, empty lines ignored), `clampDuty`, `kCapCounts`, `SweepConfig.h` |
| `lib/measure/include/measure.h`, `lib/measure/src/measure.cpp` | full | `PeriodScheduler` (origin = first call, `periodEnded` fires once even after a stall), `WindowAverager` |
| `test/test_commands`, `test/test_sweep` (grep), `test/test_sweep/sim_process.h` | partial | `Command{type, pct}` aggregate init in tests; READY/UNKNOWN COMMAND asserted only by substring; simulated process available |
| `/mnt/storage/College/CD/Proj_1/Prj1CDig.pdf` (real file behind the broken `docs/Prj1CDig.pdf` symlink), 6 pages | text extracted with `pdftotext`; figures not viewed | p.1 variables (`um`, `ym`, N = 31500, Ti = 1 s, resolution <= 0.1 degrees C), p.2 procedure step 2, p.3 resistor warning, p.4 identification procedure and `idinput` example |
| `~/.platformio/packages/framework-arduinoespressif32/cores/esp32/HardwareSerial.cpp`, `.h`, `esp32-hal-uart.c` | grep + excerpts | TX buffer 0 (writes go to the 128-byte UART FIFO and drain at line rate, no flow control), RX buffer default 256, `setRxBufferSize()` exists; `uartWriteBuf` uses `uart_write_bytes` |
| `~/.platformio/penv/lib/python3*/site-packages/platformio/test/helpers.py` | excerpt | only directories whose name starts with `test_` are suites, so a `test/helper/` folder (no `test_` prefix, no `test_*` subfolders) is ignored by `pio test` |
| `docs/sinal_prbs.csv` | full (parsed with a script, 31 501 lines) | the user's real sequence: header `Tempo_s,PWM_counts`, CRLF, `k` 0..31499, two levels 1266/1490, 158 changes, runs 100-900; drives D2 and D10 |
| `/home/thiago/.claude/skills/sdd-plan-writer/templates/plan.md` | full | no `specs/_templates/` exists; skill template used |

Installed versions checked (inferred from the machine, not from a lockfile): PlatformIO Core 6.2.0; platform `espressif32` 7.1.3 (pinned in `platformio.ini`); Arduino-ESP32 core 2.0.17 (`framework-arduinoespressif32` 4.20017.260907); `native` platform + Unity (fetched into `.pio/libdeps/native/Unity`); Python 3.12.3 with `pyserial` 3.5 present but not used (D11); no `pytest`; `tmux`, `systemd-inhibit`, `stty` present; a board is attached at `/dev/ttyACM0` right now; `gsettings ... sleep-inactive-ac-type` is `suspend` on this PC. Baseline confirmed in discovery: `pio run` succeeds (flash 22.3 %) and `pio test -e native` passes 156 of 156 tests. `pio` resolves at `/usr/bin/pio` and at `~/.platformio/penv/bin/pio`.

Not consulted (and why): `docs/LM35.PDF` and the three `docs/WhatsApp Image ...jpeg` files (sensor and wiring facts are unchanged and not touched by this feature; they were read for plan 001); `PILOT-LOG.md` (untracked pilot notes of the SDD skills, not project documentation); `../cdig-proj1-esp/` (a sibling project outside this repository); `include/README`, `lib/README`, `test/README` (PlatformIO boilerplate); `.pio/` (build output, excluded). No `docs/` Markdown exists; `docs/Prj1CDig.pdf` is now a real file (658 735 bytes, same size as `../Prj1CDig.pdf`), no longer a broken symlink; `docs/logs/*.log` (sweep logs of feature 001) were not read.

## Architecture decisions

### D1: Two new hardware-free libraries, minimal edits to the old ones
Decision: `lib/prbs` holds class `Prbs` (load, storage, playback, safety, run log lines). `lib/supervisor` holds class `Supervisor` (owns `Sweep`, `Prbs`, the command `LineBuffer` and a `PrefixSink`; routes characters, commands, period results and ticks; decides who drives the duty). Neither includes `Arduino.h`. `src/main.cpp` only forwards characters, the clock, measurements and the duty. Small additive edits elsewhere: `commands.h/.cpp` (D4), `Sweep::observe` and two text constants in `sweep.cpp` (D7, D8), `PeriodScheduler::restart` (D5).
Why: G1-G4 need logic that can be tested without the board for 8 h 45 min runs; matches 001 D1/D2. NG8 allows changing only the ready and unknown-command texts of the sweep, so the sweep stays untouched otherwise and the routing lives outside it.
Alternatives considered (not chosen): putting the run inside `Sweep` (couples two experiments, touches 156 passing tests' subject); routing in `main.cpp` (untestable on the host, and E1/E2/E10 are routing rules).

### D2: Sequence storage: 1 bit per value plus two levels (confirmed by the user, 2026-10-19)
Decision: `uint8_t bits_[kMaxValues / 8]` (4000 bytes), `uint16_t level_[2]`, `uint32_t count_`, `uint32_t changes_`. During a load the first value read becomes `level_[0]` (bit 0), the first different value becomes `level_[1]` (bit 1), a third distinct value is refused. Applied duty of second k is `level_[bit k]`, then `clampDuty()`. All-equal sequences leave `level_[1] = level_[0]`. At the end of a valid load the board prints `PRBS LEVELS low=<min of the two levels> high=<max> changes=<number of positions i where value i differs from value i-1>` (AC1), computed while loading (no extra pass or memory).
Why: NG9 and the user's memory preference; equivalent to `low + (high - low) x bit` of the spec without needing to know which level is lower while streaming. The user's file `docs/sinal_prbs.csv` is exactly this case: 31 500 values, 1266 x 16 800 and 1490 x 14 700, 158 changes, runs of 100 to 900. E5 (all equal, or a value 0) works with no special case. All options store the integers exactly, so precision is identical; the two-level check also catches a corrupted digit as a third level.
Alternatives considered (not chosen): `uint16_t` array (63 000 B for this file, 64 000 B reserved, about 15 times more RAM, only useful if NG9 were dropped); run-length list (318 B for this file, but needs a fixed run capacity and a new "too many transitions" refusal whose limit depends on the PRBS band chosen in MATLAB; `[0 .01]` gives at most 315 runs, `[0 .1]` up to 3 150); run-length on the wire (would cut the 14 s transfer but breaks AC1's "N values follow"); normalising to low/high after the load (an extra pass, no observable difference).

### D3: Load protocol, parsing and the five refusal conditions
Decision: the helper sends `prbs load N` (N decimal). The board answers `PRBS LOAD READY n=<N>` or a refusal; only after that does the helper send N lines, each a decimal integer. While loading, `Supervisor` hands every character to `Prbs::onLoadChar` (its own line assembler: strips `\r`, keeps empty lines, flags lines over 31 characters), so `LineBuffer` is untouched and E11 (blank line, `stop`, text inside the stream) is detected. A value line is valid when, after trimming spaces and tabs, it is 1 or more digits only; a leading sign, a decimal point or an exponent make it "not an integer". More than 5 digits counts as above the cap. After the N-th valid value the board enters a quiet window `kLoadTailMs = 100`: any character in it means the stream had more values than announced (refused, nothing stored); otherwise the board prints `PRBS LEVELS low=<L> high=<H> changes=<C>` and then `PRBS LOADED n=<N>`. A gap of `kLoadTimeoutMs = 5000` ms between accepted lines (measured from the announcement or the previous value) refuses with the timeout line. "Fewer values than announced" is thus detected as a non-integer or blank line, or the timeout; "more" by the quiet window. Any content refusal (AC2, E7, E11, E12) clears the stored sequence first (`hasSequence()` false); a busy refusal (D8) leaves it untouched.
Why: AC1, AC2, E7, E11, E12, A13. The board cannot otherwise know that a longer stream was sent; the 100 ms window costs one tenth of a second per load and the helper waits for the confirmation anyway (AC12).
Alternatives considered (not chosen): an explicit end marker line (adds a token the spec does not list and every client must send it); ignoring extra values (contradicts AC2). A value counts as accepted when its line terminator arrives, so the helper always terminates the last line (a file without a trailing newline would otherwise hit the 5 s timeout) and the README says so. The helper waits for `PRBS LOAD READY` before sending values, but a client that sends the values right behind the announcement also works, because the board refuses early only for E12.

### D4: Command grammar
Decision: `commands.h` gets `CmdType::PrbsLoad`, `PrbsRun`, `BadPrbs` and `Command` gets a third plain member `uint32_t count` with no default initializer (so it stays an aggregate in every C++ standard the firmware env may use, and `Command{CmdType::Start, pct}` in the existing tests still compiles with `count` zero-initialised; `make()` in `commands.cpp` sets it to 0; new firmware code never aggregate-initialises `Command`). `prbs load <N>` (digits only, up to 9 digits; more digits give `count = UINT32_MAX`), `prbs run`; any other line starting with the word `prbs` is `BadPrbs`. Same trimming, case-sensitivity and 32-byte limit as the sweep commands.
Why: Constraints (interface), spec's "exact arguments are a plan decision".
Alternatives considered (not chosen): a separate parser for `prbs` (two parsers on one line stream); `prbs load` with the first value on the same line (breaks the one-value-per-line E11 rule).

### D5: Run clock, alignment and catch-up
Decision: when a run is accepted, `Supervisor` sets a one-shot flag read with `bool takeMeasurementRestart()`; `main.cpp` then calls `PeriodScheduler::restart(nowMs)` (new: origin = now, period counter 0, sample spacing reset) and `reset()` on both averagers. Run second k is then exactly scheduler period k, and the start time `startMs_` equals the scheduler origin. `Prbs::tick(nowMs)` runs every loop pass: `elapsed = (nowMs - startMs_) / 1000` (unsigned arithmetic), duty = `level_[bit(min(elapsed, N-1))]` while `elapsed < N`, else 0. `Prbs::onPeriod` prints the pending data lines: while `nextK_ < min(elapsed, N)` print line `nextK_` (the first pending one uses the fresh period result when `samples > 0`; later catch-up lines and zero-sample results reuse the previous line's temperatures, `nan` if none, E6); `time_s = (nowMs - startMs_) / 1000` at print time. When `nextK_ == N` it prints `# RUN END ok n=<N>` and returns to idle.
Why: AC4, AC5, AC7, E6, AC11/AC15 (run length independent of reading). `PeriodScheduler::periodEnded` fires once after a stall, so counting calls would lose seconds; computing from the clock cannot.
Alternatives considered (not chosen): counting `onPeriod` calls (loses seconds after a stall); starting at the next period boundary of the running scheduler (a pending state, a delay of up to 1 s before `# RUN START`, and `stop` semantics in the gap); hardware timer interrupt for the duty (no benefit at 1 Hz).

### D6: Output channel and the `#` rule
Decision: `Prbs` prints run lines (`# RUN START`, `# columns`, data, `# RUN END`) through the raw sink. Everything else printed while a run is in progress goes through `PrefixSink`, a `LineSink` that prepends `# ` when `Supervisor` sets `prefix = prbs.running()` before each command is dispatched; `Sweep` is constructed with this sink. `PrefixSink` formats into a 160-byte buffer (the longest reply, the tolerance refusal, is about 95 characters) and truncates longer text, which a test covers. The sweep's periodic `LOG` line cannot appear because `Sweep::onPeriod` is not called during a run (D7).
Why: AC6 (only data and `#` lines between start and end; replies as `#` lines), E2 (`temp` answered as `# TEMP ...`).
Alternatives considered (not chosen): editing every `emit` in `Sweep` (touches NG8 behaviour); filtering in `main.cpp` (untestable).

### D7: Safety during a run and sharing the heater
Decision: during a run `Supervisor` calls the new `Sweep::observe(load, ambient)` (records the latest values for `temp`, no cutoff, no output; `Sweep::onPeriod` is refactored to call it first, behaviour identical) instead of `Sweep::onPeriod`, and calls `Prbs::onPeriod`. At each period result `Prbs` evaluates, in this order, using only results with `samples > 0`: load mean count 0 or 4095 -> `sensor fault load`; ambient 0 or 4095 -> `sensor fault ambient`; load `tempC >= 75.0` -> `over-temperature` (`prbscfg::kLimitC`). The check runs first on every period event, before any pending catch-up line is printed, and the result is taken as belonging to the oldest unprinted second `nextK_`. On a hit: duty 0 immediately, `# RUN END aborted k=<k> reason=<text>` where `k` is the number of data lines already printed, state `Latched`. `Supervisor::dutyCounts()` is `prbs.running() ? prbs.dutyCounts() : sweep.dutyCounts()`; the shared latch is `sweep.state() == Cutoff || prbs.latched()`. `stop` during a run calls `Prbs::stop` (duty 0 at once, `reason=stopped by user`, state back to idle, no latch).
Why: AC8, AC9, AC10, E3, E10, G3. Reusing `Sweep::onPeriod` would print `CUTOFF` lines inside the run and latch the sweep separately.
Alternatives considered (not chosen): letting `Sweep` evaluate the 80 degrees C cutoff during a run (a second, higher limit that fires after the 75 one; double reporting); an abort only on `>= 75` (misses the sensor-fault cases of AC8).

### D8: Precedence and pinned refusal texts
Decision: for `prbs load`, `prbs run` and `start` the checks run in this order and the first hit is the only line printed: (1) PRBS run in progress, (2) duty sweep in progress (HoldZero or Running), (3) latch (`reset required after cutoff`); for `start` only (1) and (3)-by-PRBS-latch are decided by `Supervisor`, a sweep in progress or a sweep cutoff is answered by `Sweep` with its existing text (`REFUSED: a sweep is already running`, `REFUSED: reset required after cutoff`), unchanged (NG8); `prbs run` then adds (4) no sequence loaded, (5) no valid measurement yet (a period with `samples > 0` on both sensors has been seen), (6) |load - ambient| above `sweepcfg::kStartToleranceC` (3.0, uncorrected). The exact texts are pinned in "Data models and interfaces". A busy or latch refusal never touches the stored sequence or the heater.
Why: AC3, E1, E2, E3, E10, A5.
Alternatives considered (not chosen): reporting all reasons (tests could not assert a single line); a different order (the spec lists no order, this one reports the most fundamental blocker first).

### D9: Data line format
Decision: `k,time_s,u_counts,load_C,ambient_C` printed with `%u,%u,%u,%.1f,%.1f`; a held value that does not exist yet is the literal `nan`. `load_C` and `ambient_C` are the `tempC` of the 0.8 s window means, uncorrected (A3). The comment line `# columns: k,time_s,u_counts,load_C,ambient_C` follows `# RUN START` once. MATLAB reads the log with `readmatrix(file,'CommentStyle','#')` (documented in the README, not tested here).
Why: AC5, A3, A7, E6.
Alternatives considered (not chosen): printing raw ADC counts as well (not in the spec, bigger lines); `%.2f` (the statement asks 0.1 degrees C).

### D10: The helper program
Decision: `tools/prbs_run.py`, Python 3 standard library only (`os`, `termios`, `select`, `signal`, `argparse`), Linux only (A11). CLI: `python3 tools/prbs_run.py --port <tty> --seq <file> --log <file>`; the baud rate is fixed at 115200. Flow: (1) open and check the log file for append, creating it (failure -> exit 2, E9, nothing sent); (2) read and validate the sequence file in either form of A7 (a) one integer per line, or (b) CSV whose first line is exactly `Tempo_s,PWM_counts` followed by `k,duty` lines with `k` = 0, 1, 2, ... without gaps; LF or CRLF; blank lines at the end ignored; the form is chosen by the first line (a header with a comma means CSV); any other shape, a non-integer, a gap in `k`, 0 values or more than 32000 values -> exit 2 without sending, E12); from the duty values it computes `low`, `high` and `changes` exactly as D2 defines them; (3) open the port (D11) and synchronise by sending `temp` until a line starting `TEMP` or `# TEMP` appears (up to 5 tries, 1 s apart), discarding boot output; (4) send `prbs load N`, require `PRBS LOAD READY n=N` within 5 s, send the N lines in 256-byte writes, require `PRBS LEVELS ...` and `PRBS LOADED n=N` within 10 s of the last value, with the count and the reported `low`, `high`, `changes` equal to the ones computed from the file, otherwise exit 2 without sending `prbs run` (AC12, AC13); both lines are shown on the console; (5) send `prbs run`, require `# RUN START` within 5 s, else send `stop` and exit 2; (6) from `# RUN START` on, write every received line to the log, flush and `fsync` each line; a line that is neither a `#` comment nor a data line of the shape `<int>,<int>,<int>,<number or nan>,<number or nan>` (for example cut or joined by bytes the kernel dropped while the reader was paused) is written prefixed with `# CORRUPT ` and counted, never dropped and never left to break `readmatrix`, and the count is printed on the console at the end; (7) end on `# RUN END ok` (exit 0), `# RUN END aborted` (exit 1) or an incomplete log (exit 3). Incomplete log reasons, appended as the last line `# LOG INCOMPLETE <reason>`: `serial error: <text>`, `no line received for 10 s`, `board reset (READY seen)`, `interrupted by <SIGINT|SIGTERM|SIGHUP>`. The "no line" check runs only when `select` timed out, never before reading what is already waiting (a long pause of the helper must not look like a silent board). Console output: the board's pre-run lines and reasons. Refusals are recognised with or without a leading `# ` (a previous run may still be playing). Exit 2 also covers every refusal and handshake failure before the run starts. Timeouts are fields of a `Timeouts` object so tests can shrink them; the CLI does not expose them. Python interface (pinned for the tests): `open_port(path: str) -> int` returns the file descriptor; `main(argv: list[str], timeouts: Timeouts = Timeouts()) -> int` returns the exit status (the `if __name__ == "__main__"` block calls `sys.exit(main(sys.argv[1:]))`); `Timeouts` is a dataclass with float fields in seconds: `sync=1.0` (between handshake tries), `ready=5.0`, `loaded=10.0`, `run_start=5.0`, `silence=10.0`.
Why: AC12, AC13, AC14, E9, E12, A9, A11, A13, G3, G4.
Alternatives considered (not chosen): `pyserial` (extra dependency with no project install convention, and it hides the DTR/HUPCL handling that A9 needs; installed 3.5 is not guaranteed on the user's other machines); a shell pipeline with `stty` and `tee` (cannot do the handshake, exit codes or the incomplete marker); filling or inventing missing lines (forbidden by AC11).

### D11: Opening and closing the port without resetting the board
Decision: `open_port(path)` is the only function that touches `termios`: open `O_RDWR | O_NOCTTY`, raw mode 115200 8N1 without hardware flow control, clear `HUPCL` immediately after opening (so closing the port, by Ctrl+C or a closed terminal, does not lower DTR/RTS), never toggle DTR/RTS itself. If the early validation (EV2) shows that merely opening the port resets the board, the fallback order is: pre-set `stty -F <tty> -hupcl` before the first open (documented in the README), then a README step to start the helper inside `tmux` (the progress notes already use it) and treat the reset on first open as harmless because the handshake (D10 step 3) runs before anything is loaded. If neither works A9 fails and the spec is reopened. The README also tells the user to start the helper under `systemd-inhibit --what=sleep:idle:handle-lid-switch` or to disable suspend (A11).
Why: A9, AC15; the observed reset on terminal close (progress.md, 2026-10-14) is the DTR drop through the auto-reset circuit.
Alternatives considered (not chosen): leaving the helper alive in the background with the port held open by a second process (more moving parts); relying only on `monitor_dtr/rts = 0` (that is PlatformIO's monitor, not the helper).

### D12: Early validation probe firmware
Decision: `tools/probe/serial_probe.cpp` built by a new `[env:probe]` (same platform and board, `build_src_filter = -<*> +<../tools/probe/>`). It prints `READY probe reset_reason=<n>` at boot and one line per second `P,<seq>,<millis>,<max_loop_gap_us>`, using the same 115200 baud and `Serial.println` path as the product. It does not touch the PWM pin. `default_envs` stays the firmware env, so `pio run` is unchanged. Commands: `pio run -e probe -t upload` (unconfirmed, created by the EV2 task, which runs first).
Why: A8, A9, A12, A14 are `[ENV]` and cannot be proven on the host; the product must not be debugged for an environment fact. The probe lines are enough to check pace (millis stamps, loop gap), reset (reason, `seq` restarting) and clock (millis vs PC clock).
Alternatives considered (not chosen): validating with the finished firmware (late, and failures would be blamed on the new code); a longer sweep (prints a line only every 5 s, no 1 s cadence).

### D13: Tests
Decision: host tests stay in Unity under `test/test_<module>/`: new `test_prbs` and `test_supervisor`; additions to `test_commands` (prbs grammar, D4), `test_measure` (`restart`) and `test_sweep` (`observe`, and the READY and UNKNOWN COMMAND texts also list the `prbs` commands; existing assertions are substring checks and stay valid, no existing test is edited). Host tests never hard-code 1266 or 1490: they use values derived from `kCapCounts` (the host build uses the default cap 25 %, 1023 counts), plus one `kCapCounts + 1` refusal. Helper tests are Python `unittest` in `test/helper/` (outside the `test_` naming that PlatformIO scans), run with `python3 -m unittest discover -s test/helper -t test/helper -v`; they talk to a fake board over a `pty` pair (`test/helper/fake_board.py`) so the whole helper protocol (the tests add `tools/` to `sys.path` to import `prbs_run`; both sequence-file forms, including a copy of the real `docs/sinal_prbs.csv` shape, are tested), exit codes and the log are tested without hardware and without `pyserial`. Commands for the helper tests are introduced by this plan (unconfirmed, created by the harness task).
Why: CLAUDE.md "test patterns `test/**`", "never weaken tests"; no second C++ framework (discovery rule); `pytest` is not installed.
Alternatives considered (not chosen): `pytest` (new dependency); testing the helper only by hand (AC12-AC14 would have no repeatable check); setting `-DSWEEP_CAP_PERCENT=50` in `[env:native]` (changes the 156 existing tests' assumptions, README says they use 25 %).

### D14: Serial receive buffer
Decision: `main.cpp` calls `Serial.setRxBufferSize(1024)` before `Serial.begin(115200)`. The load stream is never answered until its end, and the loop drains the buffer on every pass.
Why: no flow control on this link and a 256-byte default buffer (core `HardwareSerial.cpp:72`); the 32 000-value transfer is about 14 s of continuous input (spec Constraints, Transfer). Costs 768 bytes of RAM.
Alternatives considered (not chosen): keeping 256 (a loop pass delayed over 22 ms during the load could lose bytes; the 5 s and count checks would then refuse, but needlessly); pacing the helper by acknowledgements per chunk (more protocol).

## Architecture conformance
The repository has no architecture documents; the rules come from `CLAUDE.md`, the 001 plan and the spec constraints.

| Rule (source) | How the plan complies |
|---------------|-----------------------|
| Never weaken, skip, comment out or delete tests (`CLAUDE.md`) | No existing test file is edited except by adding cases; the 156 tests must keep passing; READY/UNKNOWN text changes only add words, and existing assertions are substring checks (`test_sweep.cpp:61,152,164`). |
| Never edit spec.md or plan.md without approval; do not edit generated files (`CLAUDE.md`) | Spec untouched; `CLAUDE.md` and `specs/001-*` changes are only proposed (Documentation impact). |
| Test file patterns `test/**` (`CLAUDE.md`) | Host and helper tests live under `test/` (D13). |
| Hardware-free libraries, time/sink injected (001 D1, D2) | `Prbs` and `Supervisor` take `nowMs`, `PeriodResult` and a `LineSink`; no `Arduino.h` in `lib/`. |
| One choke point for the duty and one cap (001 D5, `SweepConfig.h`, progress.md 2026-10-18) | The duty still reaches the pin only through `writeDutyCounts()` -> `clampDuty()`; `Prbs` also clamps; no second cap setting is introduced; `kCapCounts` is used for load validation and `# RUN START cap=`. |
| Sweep behaviour unchanged except ready/unknown texts and "cannot run during a PRBS run" (spec NG8) | D7: only additive `observe`, two text constants; refusal of `start` during a run is done by `Supervisor`. |
| Pins, baud, PWM channel as feature 001 (spec Constraints) | `main.cpp` constants kept; only the RX buffer size is added (D14). |
| Specs layout and Status line (`specs/README.md`) | `specs/002-prbs-experiment/plan.md`, `Status: Draft`. |
| Commit convention: none established, short imperative subject with task ID (`CLAUDE.md`) | Tasks follow it. |

## Components / files touched
| Path | Change | Notes |
|------|--------|-------|
| `lib/prbs/include/PrbsConfig.h` | new | `prbscfg::kMaxValues = 32000`, `kLimitC = 75.0`, `kLoadTimeoutMs = 5000`, `kLoadTailMs = 100`, `kLineMax = 32` |
| `lib/prbs/include/prbs.h`, `lib/prbs/src/prbs.cpp` | new | `Prbs` (D2, D3, D5, D7, D9) |
| `lib/supervisor/include/supervisor.h`, `lib/supervisor/src/supervisor.cpp` | new | `Supervisor`, `PrefixSink` (D1, D6, D7, D8) |
| `lib/sweep/include/commands.h`, `lib/sweep/src/commands.cpp` | modify | D4 |
| `lib/sweep/include/sweep.h`, `lib/sweep/src/sweep.cpp` | modify | `observe()`, `onPeriod` calls it first, READY commands line and UNKNOWN COMMAND text list `prbs load N`, `prbs run` (NG8) |
| `lib/measure/include/measure.h`, `lib/measure/src/measure.cpp` | modify | `PeriodScheduler::restart(uint32_t)` (D5) |
| `src/main.cpp` | modify | use `Supervisor`; restart scheduler and averagers when `takeMeasurementRestart()`; `setRxBufferSize(1024)`; tick every pass |
| `platformio.ini` | modify | add `[env:probe]` (D12); firmware and native envs unchanged |
| `tools/probe/serial_probe.cpp` | new | D12 |
| `tools/prbs_run.py` | new | D10, D11 |
| `test/test_prbs/test_prbs.cpp` | new | AC1 (including `PRBS LEVELS`), AC2, AC4, AC5, AC7-AC10, E5-E8, E11, E12 |
| `test/test_supervisor/test_supervisor.cpp` | new | AC3, AC6, E1-E3, E10, restart flag |
| `test/test_commands/test_commands.cpp` | modify (add cases) | `prbs load N`, `prbs run`, `BadPrbs` |
| `test/test_measure/test_measure.cpp` | modify (add cases) | `restart` |
| `test/test_sweep/test_sweep.cpp` | modify (add cases) | `observe`, new READY/UNKNOWN words |
| `test/helper/test_prbs_run.py`, `test/helper/fake_board.py` | new | AC12-AC14, E9, E12 (helper side) |
| `README.md` | modify | new section (Documentation impact) |
| `.gitignore` | modify | add `__pycache__/` |
| `CLAUDE.md` | modify (needs approval) | test and verify commands |

## Data models and interfaces
Signatures and formats only; the executor writes the bodies.

```
// PrbsConfig.h
namespace prbscfg { constexpr uint32_t kMaxValues = 32000, kLoadTimeoutMs = 5000, kLoadTailMs = 100;
                    constexpr float kLimitC = 75.0f; }

// commands.h additions (D4)
enum class CmdType { None, Start, Stop, Results, Temp, Unknown, BadStart, PrbsLoad, PrbsRun, BadPrbs };
struct Command { CmdType type; float startPercent; uint32_t count; };

// measure.h addition (D5)
void PeriodScheduler::restart(uint32_t nowMs);

// sweep.h addition (D7)
void Sweep::observe(const PeriodResult& load, const PeriodResult& ambient);

// prbs.h
enum class PrbsState { Idle, Loading, Confirming, Running, Latched };
class Prbs {
 public:
  explicit Prbs(LineSink& out);
  bool beginLoad(uint32_t count, uint32_t nowMs);        // prints PRBS LOAD READY or REFUSED; true when accepted
  void onLoadChar(char c, uint32_t nowMs);               // only while loading()
  bool requestRun(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient,
                  bool haveMeasurement);                  // checks (4)-(6) of D8; prints RUN START or REFUSED
  void tick(uint32_t nowMs);                              // load timeout, quiet window, duty of the current second
  void onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient);  // data lines, abort, end
  void stop(uint32_t nowMs);
  bool loading() const;      // Loading or Confirming
  bool running() const;      // from RUN START until the RUN END line
  bool latched() const;
  bool hasSequence() const;
  uint16_t dutyCounts() const;   // already clamped; 0 unless running
  PrbsState state() const;
};

// supervisor.h
class Supervisor {
 public:
  explicit Supervisor(LineSink& out);
  void begin(uint32_t nowMs);                             // READY lines (via Sweep::begin)
  void onChar(char c, uint32_t nowMs);
  void tick(uint32_t nowMs);
  void onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient);
  uint16_t dutyCounts() const;
  bool takeMeasurementRestart();                          // true once after a run was accepted (D5)
};
```

Pinned observables (tests assert these exactly unless a field is marked `<..>`):

| Where | Text |
|-------|------|
| Ready (`Sweep::begin`) | commands line becomes `commands: start [N] (N = 0..<cap> %), stop, results, temp, prbs load N, prbs run` |
| Unknown command | `UNKNOWN COMMAND: valid commands: start [N], stop, results, temp, prbs load N, prbs run` |
| Load accepted | `PRBS LOAD READY n=<N>`, then after the quiet window `PRBS LEVELS low=<L> high=<H> changes=<C>` and `PRBS LOADED n=<count>` (in this order) |
| Load refused: length | `REFUSED: bad length <N> (must be 1..32000)` |
| Load refused: value | `REFUSED: value <i> is not a non-negative integer` (i = 1-based position in the stream; blank lines, text and commands included) |
| Load refused: cap | `REFUSED: value <i> = <v> is above the cap <cap>` |
| Load refused: levels | `REFUSED: more than two distinct values (value <i>)` |
| Load refused: timeout | `REFUSED: no data for 5 s (got <k> of <N> values)` |
| Load refused: extra | `REFUSED: more values than announced (<N>)` |
| Busy | `prbs load`, `prbs run`, `start` during a PRBS run: `REFUSED: a PRBS run is in progress`; `prbs load`, `prbs run` during a sweep: `REFUSED: a duty sweep is in progress`; any of the three after a latch: `REFUSED: reset required after cutoff`; `start` during a sweep keeps the sweep's own text `REFUSED: a sweep is already running` |
| Run refused | `REFUSED: no sequence loaded`; `REFUSED: no measurement yet`; `REFUSED: load must be at ambient temperature first (\|load - ambient\| = <d> C > 3.0 C)` (same text as the sweep) |
| Run lines | `# RUN START n=<N> cap=<cap counts> limit=75.0`; `# columns: k,time_s,u_counts,load_C,ambient_C`; data `<k>,<time_s>,<u>,<load>,<ambient>`; `# RUN END ok n=<N>`; `# RUN END aborted k=<k> reason=<over-temperature\|sensor fault load\|sensor fault ambient\|stopped by user>` |
| While running | any reply to a command is printed with the prefix `# ` (for example `# TEMP load=...`, `# REFUSED: a PRBS run is in progress`) |
| Helper | CLI `--port`, `--seq` (either file form of A7), `--log`; exit codes 0 ok, 1 `# RUN END aborted`, 2 refusal or setup failure before the run, 3 `# LOG INCOMPLETE`; last log line `# LOG INCOMPLETE <serial error: <text> \| no line received for 10 s \| board reset (READY seen) \| interrupted by <signal>>` |

## Documentation impact
| Document | Required change | When |
|----------|-----------------|------|
| `README.md` | new section: accepted sequence files (plain list or the `Tempo_s,PWM_counts` CSV), MATLAB export and log reading (`readmatrix(...,'CommentStyle','#')`), helper usage, exit codes, log format, the 75 degrees C limit, resistor power at 0.30 W / 0.35 W per resistor against 1/4 W, Linux-only, `tmux` and `systemd-inhibit` advice (A11), reset-on-open caveat (D11), `prbs` commands in the command table, layout list | task |
| `CLAUDE.md` (SDD section) | `Test: pio test -e native` and `python3 -m unittest discover -s test/helper -t test/helper`; `Verify (all): pio run && pio test -e native && <helper test command>` (today it says Test none, Verify `pio run`) | needs separate approval (instruction file) |
| `specs/001-duty-sweep/spec.md`, `plan.md` | still say 25 % cap; spec 002 supersedes for this firmware | needs separate approval (owner's call; unchanged by this feature) |
| `docs/Prj1CDig.pdf` | broken symlink | needs separate approval (user's file layout) |
| `.gitignore` | add `__pycache__/` | task |

## Risks and mitigations
- R1: Serial output could stall the 1 s pace (A8, [ENV]). -> The core uses TX buffer 0 and writes into the UART FIFO that drains at line rate regardless of the PC; one short line per second; verified on the board in EV1 before relying on it. If it fails: the remedy is a spec-level decision (less output or a shorter window), reopen the spec.
- R2: Opening or closing the port resets the board (A9, observed on close). -> D11 clears `HUPCL`, never toggles DTR/RTS; EV2 tests 5 open/close cycles, Ctrl+C and terminal close; fallbacks listed; if all fail, reopen the spec.
- R3: Bytes lost while loading 32 000 values at 115 200 baud with no flow control. -> RX buffer 1024 (D14), loop drains each pass, helper writes 256-byte chunks; count and format checks make a loss visible (refusal), never silent.
- R4: The 100 ms quiet window could reject a client that sends the next command at once. -> The helper waits for `PRBS LOADED`; README says so.
- R5: `PeriodScheduler::restart` and the glue in `main.cpp` are only verifiable on the board. -> Unit tests cover the one-shot flag and `restart`; manual procedure M2 checks line cadence and `time_s = k+1` on the real board.
- R6: A stalled loop makes lines late. -> D5 prints them late with a larger `time_s`, tested with a simulated stall (E6).
- R7: Resistor power at the accepted levels (0.30 W / 0.35 W against 1/4 W for 8 h 45 min, spec Constraints) and LM35s powered at 3.3 V may misread above about 50 degrees C (001 README), exactly where the run lives (55 to 66 degrees C). -> Accepted by the owner; protection is the 75 degrees C limit and the cap; README states both; first run is the short M1.
- R8: Host tests built with the default cap (1023) cannot use 1266 or 1490. -> D13: values derived from `kCapCounts`; the real levels are exercised in M1/M3 on the 50 % build.
- R9: Kernel tty buffers drop bytes when the reader is paused (AC11 gap), possibly in the middle of a line. -> Expected by the spec; the helper never fills gaps (`k` shows them) and quarantines a malformed line as `# CORRUPT <line>` so the log stays readable (D10); pty test and M3 expectation cover it.
- R10: The helper is killed with SIGKILL or the PC loses power. -> No `# LOG INCOMPLETE` marker can be written (spec G3 exception); the log simply stops; README says to check the last line.
- R11: `[env:probe]` with `build_src_filter = -<*> +<../tools/probe/>` is not confirmed to compile outside `src/`. -> EV2 builds it first; fallback: place the probe under `src/probe/` excluded from the firmware env by `build_src_filter`.
- R12: Flash growth (A10) is small but unmeasured. -> The build after the firmware task reports flash use (22.3 % today); reassess only if above 50 %.

## Testing strategy
- Unit (host, `pio test -e native`, Unity): `Prbs` driven by simulated time and periods: load acceptance and every refusal (AC1, AC2, E7, E11, E12), storage and playback of a long random two-level sequence against a reference (AC4, E5), line format (AC5, D9), end (AC7), over-temperature and sensor faults (AC8), stop (AC9), cap on every simulated second (AC10), catch-up after a stall (E6). `Supervisor`: precedence and routing (AC3, E1-E3, E10), `#` prefixing (AC6), shared latch, duty ownership, restart flag. `commands`, `measure`, `sweep` additions.
- Integration (host, Python `unittest` against a pty fake board): the whole helper protocol: happy path, refusals, count mismatch, missing confirmation, missing RUN START, refusal prefixed with `# `, a malformed line in the stream, serial error, silent board, `READY` in the stream, signals, unwritable log, bad sequence file (AC12-AC14, AC15 helper side, E4 helper side, E9, E12).
- Manual (hardware, procedures below): timing and pin level on the real board, the `[ENV]` behaviours, sensor-fault abort by shorting, a full-length run.
- `[ENV]` criteria (AC11-AC15) are never proven only on the fake board: each has a manual row, the pty tests are the secondary level.
- Test locations and conventions follow: `test/test_<module>/` with Unity as in 001 (`test_commands`, `test_duty`, `test_measure`, `test_sweep`, `test_smoke`); `test/helper/` for Python.

## Verify commands
| Purpose | Command | Confirmed |
|---------|---------|-----------|
| Build | `pio run` | yes (succeeded in discovery, flash 22.3 %) |
| Test (host C++) | `pio test -e native` | yes (156 of 156 passed in discovery; `CLAUDE.md` still says Test none, see Documentation impact) |
| Test (one folder) | `pio test -e native -f test_prbs` | yes pattern (used by 001 tasks); the folder is new |
| Test (helper) | `python3 -m unittest discover -s test/helper -t test/helper -v` | unconfirmed (created by the helper harness task; see D13) |
| Build probe | `pio run -e probe` | unconfirmed (created by EV2; see D12) |
| Lint / Typecheck | none | no linter or type checker exists; the compiler with `-Wall` is the check for C++ |
| Verify all | `pio run && pio test -e native && python3 -m unittest discover -s test/helper -t test/helper` | partly unconfirmed (helper part created by the helper harness task) |

`pio` is on PATH here (`/usr/bin/pio`); in other shells use `~/.platformio/penv/bin/pio`. Run host tests without `PLATFORMIO_BUILD_FLAGS` set (README).

## Traceability
| ID | Criterion (short) | Component(s) | Test level | Also (optional) |
|----|-------------------|--------------|------------|-----------------|
| AC1 | load N values, `PRBS LEVELS`, `PRBS LOADED n=N` | `prbs.cpp`, `supervisor.cpp`, `commands.cpp` | unit | manual (M1) |
| AC2 | all refusals clear the sequence, no run | `prbs.cpp` | unit | |
| AC3 | run refused for no sequence, running, latch, tolerance | `supervisor.cpp`, `prbs.cpp` | unit | |
| AC4 | second k plays value k, one line within 0.1 s | `prbs.cpp`, `measure.cpp` (`restart`), `main.cpp` | unit | manual (M2, real timing) |
| AC5 | data line format, columns comment | `prbs.cpp` | unit | manual (M1) |
| AC6 | START/END framing, only `#` and data inside, replies as `#` | `prbs.cpp`, `supervisor.cpp` | unit | |
| AC7 | end: duty 0, `RUN END ok`, idle | `prbs.cpp` | unit | manual (M2) |
| AC8 | 75 degrees C and sensor faults abort and latch | `prbs.cpp`, `supervisor.cpp` | unit | manual (M4, shorted input) |
| AC9 | `stop` ends the run | `prbs.cpp`, `supervisor.cpp` | unit | manual (M2) |
| AC10 | duty never above the cap | `prbs.cpp`, `duty.cpp`, `main.cpp` | unit | manual (M1, multimeter) |
| AC11 [ENV] | pace unaffected by a paused reader, gap shown by `k` | `prbs.cpp`, `main.cpp`, `prbs_run.py` | manual | unit (pace is a function of the clock only) |
| AC12 [ENV] | helper reads either file form, sends, checks levels and count, runs, appends, exit codes | `tools/prbs_run.py` | manual | integration |
| AC13 [ENV] | helper stops on refusal, count or levels mismatch, timeouts | `tools/prbs_run.py` | manual | integration |
| AC14 [ENV] | serial loss or board reset -> `# LOG INCOMPLETE`, exit non-zero | `tools/prbs_run.py` | manual | integration |
| AC15 [ENV] | stopping the helper does not reset the board | `tools/prbs_run.py` (`open_port`) | manual | integration (signal handling and marker) |
| E1 | load during a run refused, run unaffected | `supervisor.cpp` | unit | |
| E2 | run or `start` during a run refused, `temp` answered as `# TEMP` | `supervisor.cpp` | unit | |
| E3 | run after cutoff latch refused | `supervisor.cpp` | unit | |
| E4 | reset during a run: sequence lost, no RUN END | `tools/prbs_run.py` | manual (M3) | integration |
| E5 | all-equal or zero values accepted | `prbs.cpp` | unit | |
| E6 | stalled period: line still printed with held values or `nan` | `prbs.cpp` | unit | |
| E7 | load interrupted -> refused after 5 s | `prbs.cpp` | unit | |
| E8 | a new load replaces the old sequence | `prbs.cpp` | unit | |
| E9 | unwritable log: helper sends nothing, non-zero exit | `tools/prbs_run.py` | integration | |
| E10 | load or run during a sweep or after a sweep latch refused | `supervisor.cpp` | unit | |
| E11 | non-integer line inside a load refuses it; CR ignored | `prbs.cpp` | unit | |
| E12 | 0 or more than 32 000 values refused; helper sends no values | `prbs.cpp`, `tools/prbs_run.py` | unit | integration |

## Environment assumptions validation

| Spec ref | Assumption | How / when validated on the real target | Early validating task | If it fails |
|----------|------------|-------------------------------------------|-----------------------|-------------|
| A8, AC11 | Serial output never delays the 1 s pace even when nobody reads | EV1 (after EV2): flash `probe`; read it only with the validated `open_port()` (or `stty -F /dev/ttyACM0 -hupcl` then `cat`); a `seq` restart or new `READY probe` line during EV1 invalidates the run and it is repeated; run 60 min; the first 10 min with no process holding `/dev/ttyACM0`, then 10 min with a reader paused via `kill -STOP`/`-CONT`; check that `seq` advances 1 per 1000 ms +/- 5 ms in the lines received afterwards and `max_loop_gap_us` stays below 50 000 | EV1 (probe; runs after EV2) | reopen spec (A8 remedy) |
| A12 | A reset is recognisable by `READY` in the stream | EV1: press EN during the probe run and watch for the `READY probe` line and `seq` restarting; then repeat with the product firmware once built (M3) | EV1 | reopen spec |
| A14 | Board clock within about 50 ppm | EV1: compare the probe's `millis` against the PC monotonic clock over the 60 min (50 ppm is 0.18 s per hour); extrapolate to 31 500 s | EV1 | adjust the 2 s tolerance of AC11/AC15 via the spec |
| A9, AC15 | Port can be opened and closed without resetting the board | EV2 (first): flash `probe` (built in EV2 as well), then with the probe running, 5 open/close cycles of `open_port()`, one Ctrl+C, one `kill -HUP` of a process in a closed terminal; reset shows as `seq` restarting or a new `READY probe` line | EV2 (runs first; creates the probe and `open_port` in `tools/prbs_run.py`) | apply D11 fallbacks in order; else reopen spec |
| A11 | PC does not suspend, USB stays up about 8.75 h | EV3: record `gsettings get org.gnome.settings-daemon.plugins.power sleep-inactive-ac-type` (today `suspend`); confirm that `systemd-inhibit --what=sleep:idle ...` shows the helper in `systemd-inhibit --list` during the EV1 run; the 8.75 h link itself is only proven by the real run (AC14 covers its failure) | EV3 | README instruction and AC14 handling stay; no spec change unless USB drops in EV1 |
| A10 | One build with both modes fits | build after the firmware task: `pio run` flash and RAM percentages | first firmware task | reassess one-build decision (A10) |

## Bring-up and safe first run
- Order: flash the product firmware with the 50 % cap build (`READY` shows `N = 0..50`); the heater is off until a run or sweep starts. Start with the short M1 sequence (low duty, 300 s) before any long run; keep the board powered from USB and the helper inside `tmux`.
- Limit damage: the duty cap (`SWEEP_CAP_PERCENT`), the 75 degrees C limit (`limit=75.0` printed in every `# RUN START`), `stop` at any time, and Ctrl+C on the helper does not stop the board (it keeps playing; send `stop` from another terminal or press EN). Do not leave a 50 % cap build running a 1490-count sequence unattended until M1 and M3 passed.
- Healthy first output: `READY`, the commands line with `prbs load N, prbs run`, `PRBS LOAD READY n=300`, `PRBS LEVELS low=400 high=600 changes=5`, `PRBS LOADED n=300`, `# RUN START n=300 cap=2047 limit=75.0`, the `# columns` line, then `0,1,<u>,<load>,<ambient>`, `1,2,...` one per second, ending with `# RUN END ok n=300`.
- README prerequisites: Linux with Python 3; user in the `dialout` group; no suspend (A11); `pio` for flashing; a sequence file holding real duty counts (plain list or the `Tempo_s,PWM_counts` CSV).

## Manual verification procedures

### M1: AC1, AC5, AC10, AC12 short first run
1. Flash the product firmware; check `READY` shows the 50 % cap and the `prbs` commands -> expected: ready lines.
2. Make `short.csv` in the same CSV form as `docs/sinal_prbs.csv` with 300 rows alternating blocks of 50 values of 400 and 600 -> expected: header plus 300 rows.
3. `python3 tools/prbs_run.py --port /dev/ttyACM0 --seq short.csv --log short.log` -> expected: console shows `PRBS LEVELS low=400 high=600 changes=5` and `PRBS LOADED n=300`, exit status 0, `short.log` has `# RUN START`, `# columns`, 300 data lines, `# RUN END ok n=300`.
4. During the run measure the PWM pin (D14) with a multimeter in DC mode -> expected: about 400/4095 and 600/4095 of 3.3 V averaged (roughly 0.3 V and 0.5 V).
5. In MATLAB `readmatrix('short.log','CommentStyle','#')` -> expected: 300 x 5 matrix, `time_s = k+1`, `u_counts` only 400 or 600.

### M2: AC4, AC7, AC9 timing, end and stop
1. Repeat M1 with 600 values; at second 100 compare the `time_s` and the PC clock -> expected: lines arrive once per second, `time_s = k+1`.
2. After `# RUN END ok`, check the pin reads 0 V and `temp` works -> expected: idle board.
3. Start another run and send `stop` from a second terminal (`tmux` split) -> expected: `# RUN END aborted k=<k> reason=stopped by user` within 1 s, helper exit 1, pin 0 V.

### M3: AC11, AC14, AC15, E4 environment behaviours (N = 1800, levels 400 and 600)
1. Start the helper in `tmux`; after 5 min send `kill -STOP <pid>`, wait 10 min, `kill -CONT <pid>` -> expected: a gap in `k`, increasing `k` afterwards, the first fresh line has `k` within 1 of the elapsed seconds, the helper does not report a silent board, the run ends `# RUN END ok n=1800` 1800 s (+/- 2 s) after it started.
2. Repeat, and press Ctrl+C in the helper at minute 5 -> expected: the last log line is `# LOG INCOMPLETE interrupted by SIGINT`, exit status 3, the board does not print `READY`; attach `pio device monitor` and see the same run continuing (strictly increasing `k`, no banner) until `# RUN END ok`.
3. Repeat, close the terminal window at minute 5 -> expected: same as 2 (no reset).
4. Repeat, press EN at minute 5 -> expected: helper appends `# LOG INCOMPLETE board reset (READY seen)`, exit 3, heater off after the reset.
5. Repeat, unplug USB at minute 5 and replug after the end -> expected: `# LOG INCOMPLETE serial error: ...` or `no line received for 10 s`, exit 3.
6. In step 1, if the log shows a `# CORRUPT ...` line where the pause cut a line, `readmatrix` must still read the file -> expected: no read error.

### M4: AC8 sensor fault (hardware), AC3, AC13 refusals
1. Start a run (M1 sequence); short the load LM35 output to GND for 2 s -> expected: `# RUN END aborted k=<k> reason=sensor fault load`, duty 0, a later `prbs run` and `start` print `REFUSED: reset required after cutoff`; press EN to recover.
2. With a file containing the value 3000 (above the 2047 cap) -> expected: helper prints the board's `REFUSED: value ... above the cap 2047`, does not send `prbs run`, exit 2.
3. Start `start` (sweep), then the helper -> expected: `REFUSED: a duty sweep is in progress`, sweep continues, exit 2; `stop` the sweep.
4. Make the load warm (more than 3.0 degrees C above ambient) and start the helper -> expected: `REFUSED: load must be at ambient temperature first ...`, exit 2.

### M5: full acceptance run
1. After M1 to M4 passed, use `docs/sinal_prbs.csv` (or a new export), start the helper under `tmux` and `systemd-inhibit` -> expected: console `PRBS LEVELS low=1266 high=1490 changes=158` and `PRBS LOADED n=31500`, then `# RUN START n=31500 cap=2047 limit=75.0`, 31 500 data lines, `# RUN END ok n=31500` after 31 500 s (+/- 2 s), exit 0, `readmatrix` returns a 31 500 x 5 matrix.

## Rollout / migration notes
- Single firmware upload; the sweep keeps working outside a run. The `READY` and unknown-command lines gain words; anything parsing them by exact text must be updated (the repository has none).
- `platformio.ini` gains `[env:probe]`; `default_envs` stays the firmware env so `pio run` behaves as before. The helper needs no installation (standard library only, Python 3 as installed here, 3.12.3; Linux).
- A board reset ends a run and loses the sequence (NG3); nothing to migrate.
