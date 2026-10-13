Status: Approved (Thiago Cardoso, 2026-10-13)

# Plan: Open-loop duty sweep for the thermal load

Spec: `specs/001-duty-sweep/spec.md` (Status: Approved (Thiago Cardoso, 2026-10-12))

## Approach summary
The firmware is split into a hardware-free core that is tested on the host and a thin Arduino glue layer that is verified on the board. Two libraries hold all logic: `measure` (count-to-degrees conversion, the 0.8 s window average, the 1 s period scheduler) and `sweep` (duty limiting, command parsing, the sweep state machine with settle detection, offset, table, interpolation, cutoff latch and all printed text). Time, ADC samples and output lines are injected, so a simulated thermal process can drive the whole sweep in milliseconds on the PC with `pio test -e native`. `src/main.cpp` only wires `analogRead`, LEDC PWM on D14, `Serial` at 115200 and `millis()` to those libraries. The measurement library has no dependency on the sweep, which gives the reusable unit asked for in G6 without adding a requirement of its own.

## Sources consulted

| Path / reference | Read | Why it matters |
|------------------|------|----------------|
| `specs/001-duty-sweep/spec.md` | full | the contract: G1-G6, AC1-AC17, E1-E14, constraints |
| `CLAUDE.md` | full | SDD section: verify = `pio run`, test = none, test patterns `test/**`, "never weaken tests" |
| `specs/README.md` | full | feature layout and Status rule |
| `platformio.ini` | full | env `esp32doit-devkit-v1`, espressif32, Arduino; no `native` env yet |
| `src/main.cpp` | full | PlatformIO stub: nothing to preserve |
| `lib/README`, `include/README`, `test/README` | headings | PlatformIO conventions: private libs in `lib/<name>/`, tests in `test/` |
| `/mnt/storage/College/CD/Proj_1/Prj1CDig.pdf` (real file behind the broken `docs/Prj1CDig.pdf` symlink) | full text, figure 1 viewed | 0 dB ADC attenuation (Vin < 1.1 V), RC filter, 12-bit PWM range, Ti = 1 s, resolution <= 0.1 degrees C, resistor power warning |
| `docs/LM35.PDF` | text through Applications Fig. 11 and p.5 charts viewed | 10 mV/degree C, variant tolerances, minimum supply vs temperature, capacitive load notes |
| `docs/WhatsApp Image 2026-10-12 at 12.17.05*.jpeg` (3 files) | viewed | DOIT-style 30-pin board, TIP120, RC filters; wire-to-pin mapping not legible |
| Arduino-ESP32 core 2.0.17 (PlatformIO `framework-arduinoespressif32` 4.20017.260907), headers `cores/esp32/esp32-hal-ledc.h`, `esp32-hal-adc.h` | headers only | LEDC API of this core is `ledcSetup` / `ledcAttachPin` / `ledcWrite`; `analogRead`, `analogSetPinAttenuation`, `analogReadResolution` |
| `~/.platformio/platforms/espressif32/boards/esp32doit-devkit-v1.json` | grep | variant `doitESP32devkitV1` |

Not consulted (and why): the remaining LM35 application circuits (Figs. 12-16), the Matlab/Simulink material the statement references (not in the repo), online Arduino-ESP32 documentation (no network use; headers of the installed version were used instead). Build output `.pio/` excluded.

## Architecture decisions
Each entry is: the decision taken, why, and the alternative that was considered but not chosen (with the reason).

- D1: Two private libraries, `lib/measure` and `lib/sweep`, plus a thin `src/main.cpp`. Neither library includes `Arduino.h`. - Rationale: the repo has no test harness (`CLAUDE.md` Test: none) and the logic is the part with 31 criteria; keeping it hardware-free makes it host-testable and keeps the measurement reusable (G6, AC14). - Alternative not chosen: everything in `main.cpp` (untestable without the board and the multi-hour process); a single library (couples measurement to the sweep, against G6).
- D2: Time, samples and output are injected (`nowMs` arguments, a `LineSink` interface, an ADC reader callback). - Rationale: lets tests simulate a 22 min to 8 h sweep in microseconds and assert exact printed lines (AC5, AC11, AC16). - Alternative not chosen: calling `millis()`/`Serial` inside the libraries (not host-buildable).
- D3: Count-to-degrees uses the ideal mapping `degC = count * (1100 / 4095) / 10` (1.1 V full scale at 0 dB, 10 mV per degree C); raw means are kept alongside. - Rationale: AC12 defines exactly this ideal conversion and Q4 requires raw counts in the output. ADC non-linearity stays a documented limitation (spec constraint). - Alternative not chosen: `analogReadMilliVolts` / eFuse calibration (hides the raw count, outside AC12, not asked for).
- D4: Sampling is cooperative: each pass of `loop()` takes one sample of each sensor when the 2 ms sample interval has elapsed inside the first 800 ms of the 1 s period, so about 400 samples per window (AC14 asks for at least 100). A period that collected zero samples (loop stalled) yields `samples = 0` and holds the previous values; `Sweep` skips AC8/AC9 evaluation and the settle history for it; `main.cpp` resets the averager after each `onPeriod` call. - Rationale: no blocking, `Serial` handling keeps running, no timers or tasks needed. - Alternative not chosen: a blocking 800 ms loop (stalls commands and cutoff) and a FreeRTOS task (extra complexity for no benefit at 1 Hz).
- D5: PWM through LEDC channel 0 at 1 kHz, 12-bit resolution, on GPIO14 (D14), written only through one function `writeDutyCounts()` in `main.cpp` that calls `clampDuty()` from `lib/sweep/duty.cpp` and contains no other logic, so the cap is enforced in host-tested code. Pins: D14 = GPIO14, D33 = GPIO33 (ADC1_CH5), D34 = GPIO34 (ADC1_CH6, input only); both ADC pins are on ADC1. - Rationale: 1 kHz x 0.8 s is exactly 800 PWM cycles per averaging window (the spec constraint that the window is long against the PWM period), fast enough for the TIP120 and far above the thermal time constants; one choke point makes AC10 provable. Pin set LOW first thing in `setup()` to shorten the boot glitch the user accepted. - Alternative not chosen: higher frequencies (no benefit, more switching noise into the LM35s); calling `ledcWrite` from several places.
- D6: Duty is tracked in counts (0..4095 scale); `percentToCounts()` rounds to nearest and `clampDuty()` limits any count to 1023 (requests of 1024, 4095 or 65535 give 1023); the table shows `count / 4095` in %. - Rationale: E8, AC10 and the 1023-count cap decided by the user. - Alternative not chosen: floating-point duty everywhere (rounding differences between output and table).
- D7: The settle test keeps a ring buffer of the last `settleWindowS` per-second (load, ambient) pairs (uncorrected; also used for the AC15 offset), cleared whenever a new step is applied, and tests `max - min <= threshold` once the step has lasted at least the window. - Rationale: exactly AC3's definition; O(window) per second is trivial. Buffer capacity is a compile-time constant (600 samples = 10 min) with a `static_assert` against the configured window. - Alternative not chosen: end-minus-start or slope (the reviewer's objection that they pass through noise zero-crossings).
- D8: Every sweep begins with a 0 % hold that ends on AC3/AC4; the offset is the mean over the window of (load - ambient) at that moment and is applied only to later reported/logged/interpolated load temperatures, never to cutoff checks. - Rationale: AC15 and Q2. - Alternative not chosen: skipping the offset when `start N` N > 0 (would break AC15's premise silently).
- D9: A line-based command parser (`start`, `start N`, `stop`, `results`, `temp`), case-sensitive, trims spaces and CR, ignores empty lines, input read from `Serial` one character at a time into a fixed 32-byte buffer. - Rationale: AC1, AC2, E4, E12, E13. - Alternative not chosen: `Serial.readStringUntil` (blocking, heap).
- D10: The sweep is a small state machine (`Idle`, `HoldZero`, `Running`, `Done`, `Cutoff`); `Cutoff` is terminal until reset. - Rationale: E1, E2, E7, E11, E14 map one-to-one to transitions; cutoff in any state (AC8) is a check made before any other transition on every period result. - Alternative not chosen: a flag-based flow (E2/E11 become implicit).
- D11: Test environment: `[env:native]` with `platform = native`, Unity (PlatformIO built-in), `build_src_filter` excluding `src/main.cpp`, and `default_envs = esp32doit-devkit-v1` so a bare `pio run` still builds only the firmware; `test_framework = unity` is stated explicitly and tests are always run with `-e native` (a bare `pio test` would target the hardware env). - Rationale: gives `Test` a runnable command in a repo with none; host compiler `g++` is present. - Alternative not chosen: tests on the board (`pio test -e esp32doit-devkit-v1`: needs hardware attached, runs for minutes).
- D12: All tunables live in `lib/sweep/include/SweepConfig.h` as constants: settle threshold 0.5, settle window 120 s, step timeout 2700 s, step 2.5 %, cap 25 %, cutoff 80, start tolerance 3.0, log period 5 s, startup bound 5 s, PWM 1 kHz. The settle values are printed in the ready message (AC1, AC3). - Rationale: AC3 "constants in one place"; Q1. - Alternative not chosen: runtime commands to change them (not requested).
- D13: The sweep steps are `N + k*2.5` for k >= 0 while the value is <= 25.0 (the last step is not forced to 25 %); `start` is N = 0. - Rationale: simplest rule consistent with AC2 "up to 25 %"; the cap protects either way; decided by the user (2026-10-13). - Alternative not chosen: adding a final 25 % step after the last multiple (an extra step the user did not ask for).

## Architecture conformance
The repository has no architecture or layering documents; the only rules come from `CLAUDE.md` and the spec.

| Rule (source) | How the plan complies |
|---------------|-----------------------|
| Never weaken, skip, comment out or delete tests (`CLAUDE.md`) | Tests are created first by the test-writer step of the executor; no existing tests exist to touch. |
| Never edit spec.md or plan.md without approval (`CLAUDE.md`) | Spec untouched; this plan only proposes doc changes (see Documentation impact). |
| Specs live in `specs/NNN-slug/` (`specs/README.md`) | Feature folder `specs/001-duty-sweep/`. |
| Test file patterns `test/**` (`CLAUDE.md`) | Host tests go to `test/test_<module>/` (PlatformIO convention). |
| Cutoff uses the uncorrected reading, duty never above cap (spec AC8-AC10) | D8 and D5/D6. |
| Pins D33, D34, D14 and 115200 baud (spec Constraints) | Fixed constants in `main.cpp`. |

## Components / files touched
| Path | Change | Notes |
|------|--------|-------|
| `platformio.ini` | modify | add `[platformio] default_envs`, `[env:native]` (Unity, src filter); in the firmware env pin `platform = espressif32@7.1.3` (LEDC API is core 2.x) and set `monitor_dtr = 0`, `monitor_rts = 0`, `monitor_speed = 115200` so reopening the serial monitor does not reset the board |
| `lib/measure/include/measure.h`, `lib/measure/src/measure.cpp` | new | conversion, `WindowAverager`, `PeriodScheduler`, `PeriodResult` |
| `lib/sweep/include/SweepConfig.h` | new | tunables (D12) |
| `lib/sweep/include/duty.h`, `lib/sweep/src/duty.cpp` | new | percent/count conversion, cap clamp, step list |
| `lib/sweep/include/commands.h`, `lib/sweep/src/commands.cpp` | new | line parser (D9) |
| `lib/sweep/include/sweep.h`, `lib/sweep/src/sweep.cpp` | new | state machine, settle, offset, table, interpolation, printing |
| `src/main.cpp` | modify (replace stub) | Arduino glue: LEDC on D14, ADC on D33/D34 at 0 dB, Serial, loop |
| `test/test_measure/test_measure.cpp` | new | AC12, AC14 |
| `test/test_duty/test_duty.cpp` | new | AC10 (`clampDuty` with 1024, 4095, 65535), E8 |
| `test/test_commands/test_commands.cpp` | new | AC2 parsing, E4, E12, E13 |
| `test/test_sweep/test_sweep.cpp` | new | AC1-AC11, AC13, AC15-AC17, E1-E3, E5-E7, E10 (`results` after a run whose output was discarded), E11, E14 with a simulated thermal process; asserts `dutyCounts() <= 1023` on every simulated period (AC10) |
| `README.md` | new | pin map, commands, run procedure, parameters (spec documentation impact) |
| `CLAUDE.md` | modify (needs approval) | SDD section values, see Documentation impact |

## Data models and interfaces
Signatures only; the executor writes the bodies.

```
// measure.h  (no Arduino.h)
struct PeriodResult { float tempC; float meanCount; uint16_t samples; };
float countToCelsius(float count);              // D3
class WindowAverager { public: void reset(); void add(uint16_t count); PeriodResult result() const; };   // 0 samples -> samples = 0, previous values held (D4)
class PeriodScheduler {                          // 1000 ms period, first 800 ms sampling, 2 ms interval
 public: bool takeSampleNow(uint32_t nowMs); bool periodEnded(uint32_t nowMs); };

// duty.h
constexpr uint16_t kFullScale = 4095, kCapCounts = 1023;
uint16_t percentToCounts(float percent);        // rounds to nearest, then clampDuty
uint16_t clampDuty(uint16_t counts);            // min(counts, kCapCounts); the only cap enforcement
float    countsToPercent(uint16_t counts);      // counts / 4095 * 100

// commands.h
enum class CmdType { None, Start, Stop, Results, Temp, Unknown, BadStart };
struct Command { CmdType type; float startPercent; };
class LineBuffer { public: bool push(char c, Command* out); };   // returns true when a line completed

// sweep.h
struct LineSink { virtual void line(const char* text) = 0; };
class Sweep {
 public:
  explicit Sweep(LineSink& out);
  void begin(uint32_t nowMs);                                  // prints the ready message (AC1)
  void onCommand(const Command& c, uint32_t nowMs);
  void onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient);
  uint16_t dutyCounts() const;                                 // already capped
};
// main.cpp only: void writeDutyCounts(uint16_t counts);       // the single LEDC write (D5)
```

Output lines are plain ASCII, one per call, fixed formats chosen in the tasks (table row, log line, cutoff, refusals).

## Documentation impact
| Document | Required change | When |
|----------|-----------------|------|
| `CLAUDE.md` (SDD section) | `Test: pio test -e native`, `Verify (all): pio run && pio test -e native` | needs separate approval (instruction file) |
| `README.md` | new: pin map as wired, commands (`start [N]`, `stop`, `results`, `temp`), parameters, run duration, 3.3 V LM35 caveat | task |
| `docs/Prj1CDig.pdf` | broken symlink (`../Prj1CDig.pdf` should be `../../Prj1CDig.pdf` or a copy) | needs separate approval (user's file layout) |
| `platformio.ini` | the user already changed the board; native env added as a component | task |

## Risks and mitigations
- R1: `pio test -e native` needs Unity, fetched by PlatformIO on first use; no network could block it. -> First task runs it on an empty test; if it fails, fall back to a header-only assert harness under `test/` (no second framework otherwise).
- R2: A `native` environment that compiles `src/main.cpp` fails (`Arduino.h`). -> D11 `build_src_filter`; verify with `pio run` and `pio test -e native` both.
- R3: ESP32 ADC non-linearity and gain error shift every absolute temperature (spec: accepted limitation); the 0 dB range is most linear below about 0.95 V, targets are 0.55-0.65 V so they sit inside it. -> Documented in README; not corrected.
- R4: LM35 on 3.3 V may misread above about 50 degrees C (spec accepted). -> README caveat; `temp` command lets the user compare against the expected 10 mV/degree C with a multimeter.
- R5: Serial output blocks if the host reads slowly, delaying sampling. -> Lines are short (<= 80 bytes, 5 s log period); 115200 baud is about 1 ms per line.
- R6: `millis()` wraps after about 49.7 days. -> Elapsed times are computed by unsigned subtraction; the longest run is about 8.25 h.
- R7: Float formatting in `snprintf` on the core. -> Verified in the first on-board task (`%.1f` of a known value); fall back to integer tenths if unsupported.
- R8: Boot-time PWM burst on D14 (accepted by the user). -> Pin driven LOW first in `setup()`; AC1 only from "startup finished".
- R9: The 0 % hold absorbs a real load/ambient difference of up to 3.0 degrees C into the offset (spec). -> README tells the user to start from a cooled load; the offset is printed in every table header.
- R10: A failed or floating load sensor is not detected (NG10); only the duty cap protects the resistors. -> Documented; cap stays the single hard limit.
- R11: Reopening the serial monitor on a DOIT-style board can reset the ESP32 through DTR/RTS (reviewer's knowledge, not verified here), which would destroy a sweep (E9). -> `monitor_dtr = 0`, `monitor_rts = 0` in `platformio.ini`, README note; the board is powered from USB during sweeps (user), so never unplug it.
- R12: `platform = espressif32` is unpinned and the LEDC calls exist only in core 2.x; a platform update could move to core 3.x and break the build. -> pin `espressif32@7.1.3` (its `platform.json` requires framework `~4.20017.0`).

## Testing strategy
- Unit (host, `pio test -e native`, Unity): conversion and averaging with injected samples; duty clamp and rounding; command parsing; the sweep state machine driven by a first-order simulated process (`T' = (Ta + K*duty - T) / tau`) with injected time and a recording `LineSink`; table, interpolation and offset assertions on exact printed text.
- Integration: none separate; the simulated-process tests are the integration of `measure` and `sweep`.
- Manual (hardware, procedures below): M1 boot behavior (AC1 timing and PWM pin 0 after startup); M2 `temp` at idle shows both sensors near ambient with samples >= 100 and the documented output format (AC14, AC17 on real hardware); M3 shorting the load sensor input to GND triggers cutoff (AC9); M4 closing and reopening the serial monitor mid-sweep (DTR/RTS reset disabled), then `results` (E10, AC16); M5 reset mid-sweep (E9); M6 a short real `start 20` run, multimeter on the PWM pin showing a gross average near 25 % of 3.3 V at most (AC10 at the pin; one count is not measurable that way).
- Test locations and conventions: `test/test_<module>/` with Unity, as PlatformIO expects; no existing tests to follow.

## Verify commands
| Purpose | Command | Confirmed |
|---------|---------|-----------|
| Build | `pio run` | yes (env parses; build not executed in discovery) |
| Test | `pio test -e native` | no, the env does not exist yet; created by the first task |
| Lint | none | n/a |
| Typecheck | none (the compiler is the check) | n/a |
| Verify all | `pio run` and `pio test -e native` | after the first task; until then `pio run` only |

`pio` is not on PATH in the shell used for discovery; it lives at `~/.platformio/penv/bin/pio`. Hardware-dependent checks (M1-M6) are manual.

## Traceability
| ID | Criterion (short) | Component(s) | Test level |
|----|-------------------|--------------|------------|
| AC1 | ready message, duty 0, waits for start (<= 5 s) | `sweep.cpp`, `main.cpp` | unit (M1 for timing) |
| AC2 | ascending steps from `start`/`start N`, start tolerance 3.0 | `sweep.cpp`, `commands.cpp`, `duty.cpp` | unit |
| AC3 | settle by max-min over window, constants printed | `sweep.cpp`, `SweepConfig.h` | unit |
| AC4 | 45 min timeout, "not settled" row | `sweep.cpp` | unit |
| AC5 | table with counts, raw counts, offset header | `sweep.cpp` | unit |
| AC6 | interpolation, ends included, lower duty on ties | `sweep.cpp` | unit |
| AC7 | unreachable target / fewer than two rows | `sweep.cpp` | unit |
| AC8 | cutoff >= 80 uncorrected, latch, any state | `sweep.cpp` | unit |
| AC9 | mean count 0 or 4095 -> cutoff naming sensor | `sweep.cpp`, `measure.cpp` | unit (M3 on hardware) |
| AC10 | duty never above 1023 counts | `duty.cpp` (`clampDuty`), `sweep.cpp`, `main.cpp` | unit (M6 gross check at the pin) |
| AC11 | log every 5 s +/- 1 s | `sweep.cpp` | unit |
| AC12 | count-to-degrees within 0.1 degrees C at five points | `measure.cpp` | unit |
| AC13 | stop -> duty 0 within one period | `sweep.cpp` | unit |
| AC14 | 0.8 s window mean, >= 100 samples, one value per period | `measure.cpp`, `main.cpp` | unit (M2 for sample count) |
| AC15 | 0 % hold, offset applied after, not to cutoff | `sweep.cpp` | unit |
| AC16 | `results` re-prints table and targets | `sweep.cpp` | unit |
| AC17 | `temp` prints latest values | `sweep.cpp`, `main.cpp` | unit (M2 on hardware) |
| E1 | start while running ignored | `sweep.cpp` | unit |
| E2 | start after cutoff refused | `sweep.cpp` | unit |
| E3 | start refused if |load-ambient| > 3.0 | `sweep.cpp` | unit |
| E4 | unknown command, parsing rules | `commands.cpp`, `sweep.cpp` | unit |
| E5 | non-monotonic table, first bracketing pair | `sweep.cpp` | unit |
| E6 | each row keeps its own ambient | `sweep.cpp` | unit |
| E7 | cutoff during a settling window | `sweep.cpp` | unit |
| E8 | rounding and clamp to 1023 | `duty.cpp` | unit |
| E9 | reset mid-sweep loses the sweep, duty 0 at boot | `main.cpp` | manual (M5) |
| E10 | host disconnected, sweep continues, `results` later | `sweep.cpp`, `main.cpp` | unit (M4 on hardware) |
| E11 | stop when not running is a no-op with message | `sweep.cpp` | unit |
| E12 | commands during printing or startup handled in order | `commands.cpp`, `main.cpp` | unit (buffer limit manual) |
| E13 | invalid `start N` refused | `commands.cpp`, `sweep.cpp` | unit |
| E14 | start after finished sweep discards old table | `sweep.cpp` | unit |

## Rollout / migration notes
- None for deployment (single firmware upload). `platformio.ini` gains an environment, and the default environment is pinned so existing `pio run` behavior is unchanged.
- Run duration is long (about 22 min to 8.25 h); README says so.

## Assumptions
- The 2 ms sample interval, 1 kHz PWM frequency, 32-byte command buffer, 600-sample settle buffer and the ASCII output formats are the planner's choices, not spec values.
- Host tests need `g++` (present) and Unity from PlatformIO (not verified offline, see R1).
- The ESP32 is powered from USB during sweeps (user, 2026-10-13); this does not restrict the plan.
- The wire-to-pin mapping (D33, D34, D14) is taken from the user's word; it could not be confirmed from the photos.
