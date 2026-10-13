Status: Approved (Thiago Cardoso, 2026-10-12)

# Spec: Open-loop duty sweep for the thermal load

Feature folder: specs/001-duty-sweep/
Source: idea from Thiago Cardoso, 2026-10-12; updated 2026-10-12 with the user's review answers, the wired circuit photos (docs/*.jpeg), the LM35 datasheet (docs/LM35.PDF) and the course statement (docs/Prj1CDig.pdf, whose symlink is broken: the real file is at ../Prj1CDig.pdf, outside the repo)

## Problem

The thermal process of the lab (a 50 ohm resistive load made of three 150 ohm resistors in parallel, switched from a 12 V source by a PWM-driven transistor, with an LM35 on the load and a second LM35 for the ambient) has no known operating point: nobody knows which PWM duty keeps the load at a given temperature. Without it, the controller design that follows cannot start from a sensible duty, and an arbitrary duty can overheat the resistors, which are rated 1/4 W each while 100 % duty at 12 V dissipates about 0.96 W in each. The student needs the firmware to measure the load temperature for a series of duties in open loop, safely, and report the duty that gives 55, 60 and 65 degrees C: 60 is the user's operating point and 55 and 65 are the operating point -5 and +5 degrees C, needed for the next part of the experiment. The LM35 outputs are small (10 mV per degree C) and sit next to a switched load, so the temperature must come from a noise-averaged reading.

## Goals

- G1: Report, for each target temperature 55, 60 and 65 degrees C, the PWM duty at which the load settles at that temperature, derived from a measured duty-to-temperature table. Duty is shown both in % and as the equivalent count of the 12-bit PWM range (0-4095).
- G2: Never harm the hardware: bound the commanded duty and stop heating on over-temperature or an invalid sensor reading.
- G3: Make the measurement observable: load and ambient temperatures reported with resolution of 0.1 degrees C or better, logged during the whole run.
- G4: Never heat unattended by surprise: nothing heats until the user starts the sweep.
- G5: (removed 2026-10-12: the circuit is already wired; the pins are now fixed constraints, see Constraints. ID not reused.)
- G6: Provide each sensor's temperature as one noise-averaged value per second, in degrees C, obtainable independently of the sweep (the later controller work will reuse the same measurement).

## Non-goals

- NG1: Closed-loop control (PI or any other). The sweep is open loop; the controller is a later work.
- NG2: Verification run that holds each found duty and confirms the settled temperature (follow-up feature).
- NG3: Compensation of the results for ambient temperature changes. Ambient is logged only.
- NG4: Persisting results (flash, SD card, network). Results go to the serial output only.
- NG5: Process identification (PRBS, model parameters) and any host-side plotting or analysis.
- NG6: Disturbance generation or fan control.
- NG7: Absolute calibration of the sensors (scale or offset against a reference). Only the relative offset between the two sensors is handled (AC15).
- NG9: Hardware or software watchdog against a firmware hang (user, 2026-10-12: out of scope).
- NG10: Detection of a disconnected or floating sensor line. The duty cap (AC10), not the cutoff, is what protects the load if the load sensor fails.
- NG8: Smoothing across measurement periods (a moving average over several seconds); the average is taken inside each 0.8 s window only.

## User stories

- US1: As the student tuning the thermal process, I want the firmware to tell me the duty that holds the load at 55, 60 and 65 degrees C, so that I have operating points for the controller design. (serves G1)
- US2: As the person leaving the setup heating for hours, I want the load to be protected from over-duty and over-temperature, so that no component burns. (serves G2, G4)
- US3: As the student, I want to see the load and ambient temperatures and each duty step as it happens, so that I can follow and trust the experiment. (serves G3)
- US4: (removed 2026-10-12: not necessary, the user wired the circuit. ID not reused.)
- US5: As the student, I want a clean temperature from each sensor once per second, usable on its own, so that measurement noise does not corrupt the experiment or the later controller project. (serves G3, G6)

## Acceptance criteria

Terms: "load temperature" and "ambient temperature" are the values derived from the LM35 on the load and on the environment. "Duty" is the PWM on-time, 0-100 %, also expressed as a count 0-4095 of the 12-bit PWM range. "Cap" is 25 % of the range, which is 1023 counts (the top of the first 1024 values of the 12-bit range). Full scale (100 %) is 4095 counts, and the % shown for a count is count / 4095. "Cutoff" is 80 degrees C. "Measurement period" is 1 second.

- AC1: Given the board has just booted, when startup finishes (within 5 seconds of power-up or reset), then the duty is 0 % (0 counts), a ready message listing the valid commands (`start`, `stop`, `results`, `temp`) and the active settle threshold and settle window is printed, and no heating starts until a start command is received. (G4, G2)
- AC2: Given the firmware is idle and the absolute difference between the uncorrected load and ambient temperatures is at most 3.0 degrees C, when the `start` command is received, then the sweep applies duties in ascending order, one step after another, from the starting point up to 25 %, in increments of 2.5 %. The starting point is 0 % for `start` and N % for `start N` (for example `start 10`). (G1, US1)
- AC3: Given a duty step has been applied for at least the settle window, when the difference between the highest and the lowest per-second load temperature over the last settle window is at most the settle threshold, then the step is recorded as settled together with the load and ambient temperatures at that moment, and the next step starts. Defaults: threshold 0.5 degrees C, window 2 minutes; both are constants kept in one place of the firmware that can be changed without altering the logic, and their active values are printed in the ready message. (G1, US1)
- AC4: Given a duty step has not settled 45 minutes after it was applied, when that time elapses, then the step is recorded as "not settled" with the last load and ambient temperatures, and the sweep continues with the next step. (G1)
- AC5: Given the last step is recorded, when the sweep ends, then within one measurement period the duty is set to 0 % and the firmware prints a table with one row per step: duty %, duty count, load temperature, ambient temperature, raw ADC count of the load sensor, raw ADC count of the ambient sensor, settled yes/no, and, in the header of every table print, the sensor offset determined in AC15. (G1, G3, US1, US3)
- AC6: Given the table is printed and a target (55, 60 or 65 degrees C) lies, ends included, between the load temperatures of two adjacent settled rows (adjacent among settled rows only, in ascending duty order, ignoring "not settled" rows), when the results are reported, then the firmware prints the target and the duty obtained by linear interpolation between those two rows (if both rows have the same temperature, the lower duty is used), as % with 0.1 % resolution and as a count (rounded to the nearest integer). (G1, US1)
- AC7: Given the table is printed and a target is above the highest or below the lowest settled load temperature, or fewer than two settled rows exist, when the results are reported, then the firmware prints that the target is not reachable within the table and no duty value for it. (G1, US1)
- AC8: Given the firmware is in any state, when the uncorrected load temperature of a period result is 80 degrees C or more (the delay from the physical event includes the RC filter and the window average), then the duty is 0 %, the firmware prints a cutoff message with the temperature, the sweep is aborted, and the duty stays 0 % until the board is reset. (G2, US2)
- AC9: Given a sensor (load or ambient) whose window mean raw count is 0 or 4095 (input shorted to ground or saturated), when the period result is produced, then the same cutoff behavior as AC8 happens and the message names the sensor. (G2, US2)
- AC10: Given any state of the firmware, when the output is observed (every step, log line, table row and the PWM pin), then the duty never exceeds the cap (25 %, 1023 counts of 4095, on-time at the pin never above 1023/4095). The duty-setting path is also verified by a test that requests values above the cap and expects the cap. (G2, US2)
- AC11: Given a sweep is running (from the start command until the table is printed), when 5 seconds (+/- 1 second) elapse, then a log line is printed with the time in seconds since the start command, current duty in % and in counts, load temperature, ambient temperature and the raw ADC count of each sensor (window mean, one decimal), temperatures shown with one decimal. (G3, US3)
- AC12: Given the raw ADC count that an ideal 12-bit converter with a 1.1 V full scale produces for 0.250, 0.550, 0.600, 0.650 or 0.800 V (an ideal LM35 at +10.0 mV per degree C), when the firmware converts that count, then the temperature is 25.0, 55.0, 60.0, 65.0 or 80.0 degrees C respectively, within 0.1 degrees C. This criterion covers the count-to-degrees conversion only; the ESP32 ADC's own non-linearity and gain error are outside it. (G3)
- AC13: Given the sweep is running, when the stop command is received, then within one measurement period the duty is set to 0 %, the sweep ends without printing results, and the firmware returns to idle. (G2, G4, US2)
- AC14: Given a sensor and a measurement period of 1 second, when the period runs, with or without a sweep running, then the sensor is sampled at least 100 times during the first 0.8 s of it, and the temperature reported for that period is the arithmetic mean of the samples of that window converted to degrees C, without the offset of AC15; the mean of the raw 12-bit ADC counts of the same window is the count shown in the table (AC5) and the log (AC11), and the latest period result with the number of samples used is shown by the `temp` command (AC17); exactly one value per sensor per period is produced and no value from earlier periods is mixed in. (G3, G6, US5)
- AC15: Given a sweep has started, when the load has been held at 0 % until it settled (AC3) or timed out (AC4), then the sensor offset is computed as the mean over the settle window of (load temperature - ambient temperature), printed, and subtracted from every later load temperature (log lines, table rows, interpolation); it is not applied to the cutoff checks of AC8 and AC9, which use the uncorrected reading. The initial hold at 0 % is a table row only when the starting point is 0 %. (G3, G1, US3)
- AC16: Given the firmware is idle or a sweep is running, when the `results` command is received, then the firmware prints the table of the steps recorded so far (AC5 format) and, if the sweep has ended without cutoff or stop, the target duties of AC6 and AC7; if no step is recorded yet it prints that there are no results. The table stays available until the board is reset. (G1, G3, US1)
- AC17: Given the firmware is in any state, when the `temp` command is received, then it prints the latest period result of the load and ambient sensors: uncorrected temperature in degrees C with one decimal, the window mean raw count, and the number of samples of the window. (G3, G6, US5)

## Edge cases and error handling

- E1: Start command while a sweep is running -> ignored, firmware prints that a sweep is already running.
- E2: Start command after a cutoff -> refused, firmware prints that a reset is required.
- E3: Start command while the absolute difference between the uncorrected load and ambient temperatures is more than 3.0 degrees C -> refused, firmware prints that the load must be at ambient temperature first, duty stays 0 %.
- E4: Unknown serial command -> firmware prints the list of valid commands; state and duty unchanged. Commands are case-sensitive, trimmed of spaces and CR, and empty lines are ignored.
- E5: The measured load temperature is not monotonic in duty (a later step settles colder than an earlier one) -> the interpolation for a target uses the first adjacent pair, in ascending duty order, that brackets the target.
- E6: Ambient temperature changes during the sweep -> each table row shows its own ambient temperature; no correction is applied.
- E7: Cutoff condition appears during the settling window of a step -> AC8 wins; the step is not recorded as settled.
- E8: A step duty (2.5 % multiples) is not an integer count of the 12-bit range -> it is rounded to the nearest count and never exceeds the cap, so the 25 % step is applied as 1023 counts (24.98 %); the table shows the % of the count actually applied.
- E9: Reset or power loss during a sweep -> the sweep is lost; at the next boot the duty returns to 0 % and the firmware waits for a start command (AC1).
- E10: Serial host not connected, or disconnected, during a sweep -> the sweep continues and the table is kept in memory; the `results` command prints it again after reconnecting (AC16).
- E11: Stop command while idle, after a cutoff, or after the sweep ended -> no state change, duty stays 0 %, firmware prints that nothing is running.
- E12: Any command received while the table or results are being printed, or before startup has finished -> handled in arrival order after the printing or the startup has finished; input beyond the serial buffer may be dropped.
- E13: `start N` with N missing a number, negative, or above 25 -> refused, firmware prints the valid form (`start` or `start N`, N from 0 to 25); state and duty unchanged. A N that is not a multiple of 2.5 is accepted and rounded to the nearest count (E8).
- E14: `start` while idle with a table from an earlier sweep -> accepted; the previous table and offset are discarded when the new sweep starts.

## Constraints

- Hardware (given environment, user, 2026-10-12): ESP32-WROOM-32 module on a 30-pin DOIT-style DevKit board (PlatformIO board `esp32doit-devkit-v1`, set in `platformio.ini` by the user on 2026-10-12); Arduino framework; load switched by a TIP120 from 12 V.
- Supply (user, 2026-10-12): the LM35 sensors are powered at 3.3 V, below the 4 V minimum of the datasheet; the user accepts the risk and will change it if it becomes a problem, so the specified conversion accuracy is not guaranteed by the sensor datasheet. The datasheet's minimum-supply-versus-temperature chart (LM35.PDF p.5) rises with temperature and, as read by the reviewer, reaches roughly 3.3 V near 50 degrees C, so readings in the 55-65 degrees C target range may be wrong, not only less accurate (user accepted, 2026-10-12). An RC low-pass filter is on each sensor line (statement figure 1, circuit photos).
- Commands (user, 2026-10-12): serial interface with commands `start [N]`, `stop`, `results` and `temp`, 115200 baud, newline-terminated.
- Boot (user, 2026-10-12): a brief glitch of the PWM output while the board boots or resets is acceptable (the D14 pin may emit PWM at boot); AC1 applies once startup has finished.
- Pins (fixed by the existing wiring, confirmed by the user, not verifiable from the photos; labels as printed on the board): load LM35 -> D33, ambient LM35 -> D34, TIP120 base (PWM) -> D14.
- Sensors: LM35, linear +10.0 mV per degree C (datasheet). The relative offset (AC15) absorbs any real difference between load and ambient present during the 0 % step (up to the 3.0 degrees C allowed by AC2/E3), so the user should start from a cooled load. The sensor variant is LM35DZ (statement figure 1, confirmed by the user; the part marking is not checked): rated 0 to 100 degrees C, accuracy at +25 degrees C of +/-0.6 typical and +/-1.5 maximum, scale tolerance +/-2 % (datasheet). The 3.0 degrees C limit of AC2/E3 equals the worst case of two such sensors (2 x 1.5), so a start can be refused with both at equilibrium. The +/-0.1 degrees C of AC12 is the accuracy of the conversion only, not of the sensors. The sensors are not calibrated and no reference thermometer is available (Q2 resolved: relative offset only).
- Temperature resolution: 0.1 degrees C or better (statement, section 1). Known limitation: the ESP32 ADC's non-linearity and gain error are not corrected (user, 2026-10-12: conversion accuracy only).
- Measurement (user, 2026-10-12): 1 s period, 0.8 s sampling window, mean of the samples as the reading. The window must be long compared with the PWM period so that switching noise averages out. The measurement must be obtainable without the sweep or safety logic (AC14); how it is packaged for reuse is a plan decision.
- Duty cap 25 % and cutoff 80 degrees C (decided by the user, 2026-10-12). AC9 range, AC4 timeout, AC11 log period, AC13 and E3 (its 3 degrees C limit) confirmed by the user on 2026-10-12.
- Settling criterion: default 0.5 degrees C over 2 minutes, configurable (Q1 resolved).
- Sweep order is ascending from the starting point, so no cool-down between steps is required. Duration: at least about 22 minutes (11 steps, 2 minutes each) and at most about 8.25 hours (11 steps, 45 minutes each); starting above 0 % shortens it, and the initial 0 % hold adds at least the settle window.
- Documentation language: English.

## Documentation impact (flags only)

- README (new) or an equivalent document -> pin map as wired, how to run the sweep (commands including `start N`, `results` and `temp`, expected duration), and the configurable parameters.
- platformio.ini -> board and env changed from `upesy_wroom` to `esp32doit-devkit-v1` (done by the user's request, 2026-10-12).
- docs/Prj1CDig.pdf -> broken symlink (points to `../Prj1CDig.pdf`); docs/LM35.PDF and docs/*.jpeg (circuit photos) are now part of the project documentation.

## Open questions

- Q1: Settling criterion accuracy (0.5 degrees C over 2 minutes leaves up to about 1.2 degrees C below equilibrium) -> RESOLVED (2026-10-12): keep 0.5 degrees C / 2 minutes as the default; the threshold and the window are configurable parameters so they can be tightened later (AC3).
- Q2: Sensors are not calibrated and no reference thermometer exists -> RESOLVED (2026-10-12): no absolute calibration (NG7). Instead the relative offset between load and ambient sensor is computed during the 0 % step, when both are at ambient temperature (AC15).
- Q3: The "moving average" of the reading (AC14) -> RESOLVED (2026-10-12): only the mean of the samples inside the 0.8 s window; no averaging across seconds (NG8).
- Q4: "A number derived from the 12 bits of the ESP" -> RESOLVED (2026-10-12): the duty is shown in % and as a 12-bit count (G1), and the raw 12-bit ADC count of each sensor (window mean) is shown in the table and log (AC5, AC11, AC14).
- Q5: Serial host not connected or disconnected during the sweep (E10) -> RESOLVED (2026-10-12): option (b): the sweep continues, the table stays in memory until reset and the `results` command prints it again (AC16, E10). Pushing to a Google Sheet is not part of this feature (NG4 stays).
