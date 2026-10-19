Status: Approved (Thiago Cardoso, 2026-10-19)

# Spec: Open-loop PRBS experiment and data log

Feature folder: specs/002-prbs-experiment/
Source: idea from Thiago Cardoso, 2026-10-18; course statement docs/Prj1CDig.pdf (now a regular file in `docs/`), cited by page: p.1 (variable table and design specifications), p.2 (procedure step 2), p.3 (sections 3 and 4), p.4 (identification procedure and Figure 3); earlier results in specs/001-duty-sweep/ (duty sweep table, progress.md).

ID convention: a removed ID stays as a tombstone, e.g. `G5: removed (<reason>); ID not reused`.

## Problem

The identification step of the course project needs the open-loop response of the thermal process to a pseudo-random binary input: the input actually applied (`um`), the load temperature (`ym`) and the ambient temperature (`Ta`), sampled every 1 s (Ti) for N = 31 500 samples, about 8 h 45 min (statement p.1 and p.2 "typically 8 h of collection"). The firmware built so far only does the duty sweep (feature 001): it cannot play a given input sequence at a fixed 1 s pace, and nothing records a run. Doing it by hand is not workable: 31 500 values must be applied on time for hours, and a closed terminal has already been seen to reset the board during a long run (specs/001-duty-sweep/progress.md), losing everything. The student needs to load the sequence generated in MATLAB, let the board play it unattended, and get one clean, MATLAB-readable log line per second.

## Goals

- G1: Apply a user-supplied input sequence (the PRBS generated in MATLAB) to the heater, one value per second, for the whole length of the sequence.
- G2: Record, for every second of the run in which a reader is attached, the applied input and the load and ambient temperatures in a log file on the PC that MATLAB reads directly (`um`, `ym`, `Ta`); seconds printed while no reader is attached are lost, and the log then shows a gap identified by `k` (AC11).
- G3: Keep the 8 h 45 min run safe: bounded duty, an over-temperature limit, heater off on any end of the run, and a log that shows whether the run finished or not (unless the helper itself is killed or the PC loses power).
- G4: Make loading and recording repeatable without typing: the sequence is checked when loaded, the transfer is confirmed, and the log is written as the run goes.

## Non-goals

- NG1: Generating the PRBS (done by the user in MATLAB, `idinput(31500,'PRBS',[0 .01],[1266 1490])`) and choosing its levels or time scale.
- NG2: Identification and controller design (`procest.m`, `sisotool`, Simulink).
- NG3: Resuming a run after a reset or a power loss, and keeping the sequence across a reset (follow-up feature).
- NG4: Aborting the run when the PC link is lost: the board cannot tell, and the user chose to let it finish (A9).
- NG5: Correcting the sensor offset or calibrating the sensors; the log holds raw values and the user corrects offline.
- NG6: Closed-loop control and the disturbance fan.
- NG7: Real-time plotting or any analysis of the log.
- NG8: Changing the behaviour of the duty sweep (feature 001), except that it cannot run during a PRBS run and that its ready message and unknown-command reply will also list the `prbs` commands.
- NG9: Sequences with more than two distinct values (multi-level or arbitrary signals); only two-level (binary) sequences are supported (user preference to store one bit per value and save memory; 32 000 sixteen-bit values, about 64 kB, would also fit in RAM, so this is a choice, not a necessity).

## User stories

- US1: As the student, I want to load my MATLAB PRBS sequence into the board and start the run, so that the process is excited exactly as I designed it. (serves G1, G4)
- US2: As the student, I want one line per second with the applied input and both temperatures in a file MATLAB can read, so that I can identify the model with `procest`. (serves G2)
- US3: As the person leaving a run going for almost nine hours, I want the heater to be protected and every way a run can end to be visible in the log, so that I can trust the data or discard it. (serves G3)
- US4: As the student, I want a helper that sends the file and records the log for me, so that I do not have to move 31 500 values or capture the output by hand. (serves G4)

## Acceptance criteria

Each criterion names its observation point: a serial output line of the board, the log file on the PC, or the exit status of the helper program. "Duty" means the PWM on-time as a count of the 12-bit range (0 to 4095); "cap" is the maximum duty of the current firmware build (50 % = 2047 counts in this project, A10). "Run" is one playing of a loaded sequence.

- AC1: Given the board is idle and no run is in progress, when the user sends the load command announcing N values followed by N integer duty values (each from 0 to the cap), then the board prints the line `PRBS LEVELS low=<L> high=<H> changes=<C>` (the lower and the higher of the stored values, and the number of positions where a value differs from the one before it; for a sequence with one distinct value, `low` equals `high` and `changes=0`), followed by the confirmation line `PRBS LOADED n=<count>` with the number of values it accepted, equal to N. (G4, US1)
- AC2: Given a load whose sequence has a value above the cap, a value that is not an integer, a length of 0, a length above 32 000 values (A6), more than two distinct values (NG9), fewer or more values than announced, or no value arriving for 5 seconds, when the load ends or times out, then the board prints a refusal line starting `REFUSED:` with the reason, stores nothing, discards any sequence loaded before, and a run cannot start until a valid sequence is loaded. (G4, G3, US1)
- AC3: Given the board has no valid sequence loaded, or a run is in progress, or a cutoff is latched, or the absolute difference between the uncorrected load and ambient temperatures is more than 3.0 degrees C, when the run command is sent, then it is refused with a line starting `REFUSED:` that names the reason, and the heater stays at 0 %; otherwise the run starts and the board prints the run-start line (AC6). (G1, G3, US1; A5)
- AC4: Given a run of N values has started, when it is in progress, then for each k from 0 to N-1 the duty applied during second k is value k of the sequence, changing at the start of that second, and exactly one data line for k is printed within 0.1 s after the end of second k, by the board's own clock. (G1, G2, US1)
- AC5: Given a run in progress, when second k ends, then the data line is `k,time_s,u_counts,load_C,ambient_C` (comma separated, no spaces), where `time_s` is the whole seconds since the run start by the board's clock at the end of second k (k+1 when the line is on time, larger if it is printed late), `u_counts` is the duty applied during second k, and `load_C` and `ambient_C` are the 0.8 s window means of second k (same measurement as the duty sweep), uncorrected, with one decimal; a comment line `# columns: k,time_s,u_counts,load_C,ambient_C` is printed once before the first data line, and `time_s` is an integer. (G2, US2; A3)
- AC6: Given a run starts and later ends, when the log is read, then it starts with a line `# RUN START n=<N> cap=<cap counts> limit=75.0` and ends (unless AC14 applies) with exactly one of `# RUN END ok n=<N>` or `# RUN END aborted k=<k> reason=<text>`; lines beginning with `#` are comments, all other lines are data lines. From `# RUN START` to `# RUN END` the board prints only data lines and `#` lines: the sweep's periodic log line is not printed and every reply to a command (`temp`, refusals) is printed as a `#` line. (G3, G2, US3)
- AC7: Given the last value of the sequence has been applied for its whole second and its data line printed, when that happens, then within 1 s the duty is 0 %, the line `# RUN END ok n=<N>` is printed and the board is idle again, ready for a new load or for the duty sweep. (G1, G3)
- AC8: Given a run in progress, when the uncorrected load temperature of a period result reaches 75.0 degrees C or more, or a sensor's window-mean count is 0 or 4095, then within one measurement period the duty is 0 %, the line `# RUN END aborted k=<k> reason=<over-temperature | sensor fault load | sensor fault ambient>` is printed (`k` is the index of the second in progress when the abort happened, and the data line of that second is not printed), the run ends, and further runs and sweeps are refused until the board is reset. (G3, US3; A4)
- AC9: Given a run in progress, when the user sends `stop`, then within 1 s the duty is 0 %, the line `# RUN END aborted k=<k> reason=stopped by user` is printed and the board is idle. (G3, US3)
- AC10: Given any run, when its log is read, then no `u_counts` value exceeds the cap, and the duty at the pin is limited by the same cap. Observation points: the log, and the pin level measured with a multimeter in the manual check (the clamp itself is covered by feature 001's tests). (G3; A10)
- AC11 [ENV]: Given a run in progress, when the PC stops reading the board's serial output for 10 minutes and then resumes, then the lines printed meanwhile may be missing (the log has a gap, and `k` shows exactly which seconds are missing), but the board's pace is unaffected: lines received after the resume have strictly increasing `k`, the first fresh line has `k` equal to the seconds elapsed since the run start (within 1 second; older buffered lines may arrive first), and the run ends with `# RUN END ok n=<N>` N seconds after it started, within 2 seconds (A14). The helper does not invent or fill missing lines. (G1, G2, US3; A8, A14)
- AC12 [ENV]: Given the helper program, a sequence file (A7), the log file path and the serial port, when the user runs it, then it sends the load command and the values, shows the board's levels and confirmation lines, checks that the confirmed count equals the number sent and that the reported `low`, `high` and `changes` equal the ones computed from the file, sends the run command, and appends every line the board prints from `# RUN START` onward to the log file as it arrives (the earlier confirmation lines are shown on the helper's console, not written to the log) (the file on disk is up to date within 2 seconds of each line), until the run-end line; it exits with status 0 only after `# RUN END ok`, and with a non-zero status after `# RUN END aborted` or `# LOG INCOMPLETE`. (G4, G2, US4; A11)
- AC13 [ENV]: Given the helper program, when the board refuses the load or the run, or the confirmed count, `low`, `high` or `changes` differ from the file, or no confirmation arrives within 10 s of the last value sent, or no `# RUN START` line arrives within 5 s of the run command, then the helper does not send the run command (or, for the last case, reports the failure), prints the board's reason, leaves the heater idle and exits with a non-zero status. (G4, G3, US4)
- AC14 [ENV]: Given a run in progress, when the serial connection is lost (a serial error, or no line received for 10 s, since the board prints one line per second) or the board resets (the `READY` line appears in the stream), then the helper keeps the lines already written, appends `# LOG INCOMPLETE <reason>` as the last line, and exits with a non-zero status; the log has no `# RUN END` line. (G3, G2, US3, US4; A9, A12)
- AC15 [ENV]: Given a run in progress, when the helper (or any serial terminal) is stopped with Ctrl+C or its terminal window is closed, then the board is not reset: after a new reader is attached, the lines received continue the same run (strictly increasing `k`, the first fresh line with `k` equal to the seconds elapsed since the run start within 1 second, a gap allowed as in AC11, no start-up banner) and the run still ends with `# RUN END ok n=<N>` within 2 seconds of N seconds after its start. (G3, G1, US3; A9, A14)

## Assumptions

Listed for confirmation. Environment items are verified early in the plan (the criteria tagged [ENV] reference them).

- A1: The sequence is generated by the user in MATLAB with `idinput(31500,'PRBS',[0 .01],[1266 1490])`: N = 31 500 values of two levels, 1266 and 1490 counts (30.9 % and 36.4 % of full scale), sampling period Ti = 1 s. — source: user (2026-10-18)
- A2: The run starts from rest, at once at the first value; the first 2 000 samples are the transient to the operating point and are discarded in MATLAB (`transit`, statement p.1); the board adds no warm-up phase. — source: user, statement p.1
- A3: The temperatures logged are the raw window means (no sensor offset correction), one decimal, which satisfies the 0.1 degrees C resolution of the statement (p.1); the user subtracts the sweep's offset (0.61 degrees C) in MATLAB if wanted. — source: user (2026-10-18)
- A4: The over-temperature limit during a run is 75.0 degrees C (the sweep's 80 degrees C cutoff stays for the sweep); the expected load temperature during the run is about 55 to 66 degrees C, so the limit sits 9 to 20 degrees C above it. — source: user (2026-10-18)
- A5: A run may start only when the load is within 3.0 degrees C of the ambient temperature, as for the sweep. — source: user (confirmed 2026-10-18)
- A6: The board accepts sequences of up to 32 000 values (the statement's N = 31 500 plus a safety margin); a longer sequence is refused. — source: user (2026-10-18)
- A7: The sequence file holds the real duty counts (here 1266 and 1490) in one of two plain-text forms, with LF or CRLF line ends and blank lines at the end ignored: (a) one integer per line, as MATLAB `writematrix(u,'prbs.txt')` writes `u`; or (b) the CSV the user actually has, `docs/sinal_prbs.csv`: a first line `Tempo_s,PWM_counts`, then `k,duty` lines where `k` runs 0, 1, 2, ... without gaps. The helper checks the header and the `k` column and sends only the duty values; any other shape is refused by the helper before anything is sent. The same values are `um` for identification; the log file is read in MATLAB with `readmatrix` using `#` as the comment character. The user's file has N = 31 500 values, 16 800 of 1266 and 14 700 of 1490, 158 changes, runs of 100 to 900 values. — source: model-proposed (log reading); user (2026-10-18: two-level sequence, stored compactly, see Constraints; 2026-10-19: the file `docs/sinal_prbs.csv`, helper to accept it, board to report the levels)
- A8: [ENV] The board's serial output never delays its 1 s pace, even if the PC reads slowly or not at all for minutes (a blocked transmit buffer could otherwise stall the loop). Not yet verified on this board. If it proves false, the remedy is a plan decision (less output per second, or a shorter measurement window than feature 001's 0.8 s, which would need its own approval). — source: environment
- A9: [ENV] If only the PC link is lost the board cannot tell and plays the sequence to its end; the log then stops where the PC stopped reading. A board reset ends the run and loses the sequence (RAM only, NG3). A closed terminal was observed to reset this board; AC15 requires that stopping the helper does not. Opening a serial port can itself reset some ESP32 boards (DTR/RTS), and the project's monitor settings only cover `pio device monitor`: the helper must open the port without that effect, verified early. — source: user (2026-10-18), environment
- A10: The firmware keeps one build with both modes (sweep and PRBS run) and the single duty-cap setting of feature 001; this project's firmware build has the cap at 50 % (2047 counts), and the levels 1266 and 1490 counts are below it; if the firmware grows too big, the one-build decision is reassessed. — source: user (accepted 2026-10-18)
- A11: [ENV] The PC runs the helper (Python, the user's choice) for the whole run without suspending, the USB link stays up for about 8.75 hours, and the board is powered from USB for the whole run; the user configures the PC (Pop!_OS) not to suspend, with instructions given in chat and in the README. — source: environment, user (2026-10-18)
- A12: [ENV] A board reset can be recognised in the serial stream by its start-up line `READY` (feature 001, AC1). — source: environment
- A13: The helper's timeouts (10 s for the load confirmation, 5 s for the run start, 10 s without a line during a run) and the 5 s no-data timeout on the board. — source: model-proposed
- A14: [ENV] The board's clock is accurate to about 50 ppm, which is about 1.6 s over the 31 500 s run (hence the 2 s tolerance on the run length). — source: environment (typical crystal accuracy, not verified)

## Edge cases and error handling

- E1: Load command while a run is in progress -> refused with a line naming the reason; the running sequence and the heater are unaffected.
- E2: Run command while a run is in progress, or a sweep command (`start`) during a run -> refused ("already running"); `temp` is still answered, as a `# TEMP ...` line.
- E3: Run command after a cutoff latch (AC8) -> refused until the board is reset.
- E4: Board reset during a run -> heater 0 % at boot, the sequence is lost, no `# RUN END` line (AC14 on the PC side); a new load is needed.
- E5: Sequence with all values equal, or with any value 0 -> accepted and played as given.
- E6: A measurement period with no samples (stalled loop) during second k -> the data line for k is still printed, with the temperatures held from the previous second (or `nan` when k = 0), so the log has N lines; a second that is late is printed late, never skipped.
- E7: Load interrupted halfway (PC stops sending) -> refused after 5 s without data (AC2), nothing stored.
- E8: The user loads a new sequence after a finished or aborted run -> the previous sequence is replaced.
- E9: The log file path cannot be written (helper) -> the helper stops before sending anything and exits with a non-zero status.
- E10: Load or run command while a duty sweep is in progress, or after a sweep cutoff latch -> refused with a `REFUSED:` line naming the reason; the sweep and the heater are unaffected.
- E11: A line in the middle of a load stream that is not an integer (a command such as `stop`, a blank line, text) -> the load is refused (AC2) and nothing is stored; a trailing CR on a value line is ignored.
- E12: A load announcing 0 values, or more than 32 000 -> refused immediately, before any value is accepted; the helper stops without sending values.

## Constraints

- Hardware and wiring: same board, pins, sensors and PWM channel as feature 001 (given environment); sensors at 3.3 V with the accepted risk recorded there.
- Timing: sampling period 1 s, the temperature is the 0.8 s window mean of the same second (feature 001, AC14); the full run is 31 500 s = 8 h 45 min.
- Transfer: even sent as text, 32 000 values of up to 4 digits plus a line end are about 160 kB, which at 115 200 baud (about 11.5 kB/s) takes about 14 s; the 5 s no-data timeout of AC2 applies per value gap, not to the whole transfer.
- Log size: about 35 bytes per line, 31 500 lines, about 1.1 MB.
- Interface: the existing serial protocol (115 200 baud, newline-terminated text, case-sensitive commands) is extended with a command to load a sequence (`prbs load`, announcing the number of values N, which are then sent as lines; the exact arguments are a plan decision), a command to run it (`prbs run`) and the existing `stop`; the sweep commands keep their behaviour outside a run. The user types none of this: the helper (AC12) sends it. The spec pins the line prefixes and column names used above; the plan pins the remaining formats (the arguments of `prbs load`, number formats not stated).
- Sequence shape (user, 2026-10-18): the sequence is two-level (a PRBS): its values take at most two distinct values, low and high, which may be any counts up to the cap.
- Plan input, not a requirement (user preference, 2026-10-18): store the sequence as one bit per value with the two levels kept separately, and apply `duty = low + (high - low) x bit` for each second, to reduce memory use.
- Cap precondition: a run needs a duty cap at or above the sequence's high level; a firmware built with the default 25 % cap (1023 counts) refuses this experiment's levels (AC2). For this project's firmware the cap is 50 % (2047 counts), accepted by the owner on 2026-10-18 and recorded in specs/001-duty-sweep/progress.md; it supersedes feature 001's default of 25 % for this firmware (the spec and plan of feature 001 still say 25 % until the owner updates them).
- Resistor power (accepted risk, owner Thiago Cardoso, 2026-10-18): the levels dissipate about 0.30 W (1266 counts) and 0.35 W (1490 counts) per 150 ohm resistor, against a 1/4 W rating, for 8 h 45 min (0.96 W x duty fraction). The statement itself warns that 1/4 W resistors burn easily outside a suitable PWM range (p.3). Protection: the 75.0 degrees C limit and the duty cap.
- Documentation language: English.

## Documentation impact (flags only)

- README.md -> new section: how to generate and export the sequence in MATLAB, how to run the helper, the log format, the 75 degrees C limit, the resistor-power caveat, and how to stop Pop!_OS from suspending during the run.
- CLAUDE.md (SDD section) -> a test command for the helper program, and the host test command (`pio test -e native`), if the plan adds them.
- specs/001-duty-sweep/ -> unchanged; the single duty-cap setting and the shared measurement are reused.
- docs/Prj1CDig.pdf -> no change (it is now a regular file, no longer a broken symlink); the statement is cited by page above.

## Open questions

None
