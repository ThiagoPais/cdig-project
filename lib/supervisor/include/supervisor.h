#pragma once
#include <stdint.h>

#include "commands.h"
#include "measure.h"
#include "prbs.h"
#include "sweep.h"

// Passes lines through, or prepends "# " to each of them while the prefix is on (plan D6): every reply printed
// while a PRBS run is in progress must be a comment line of the log.
class PrefixSink : public LineSink {
public:
    explicit PrefixSink(LineSink& out) : out_(out) {}
    void setPrefix(bool on) { prefix_ = on; }
    void line(const char* text) override;

private:
    LineSink& out_;
    bool prefix_ = false;
};

// Owns the serial console and decides who drives the heater: the duty sweep or a PRBS run (plan D1, D7, D8).
// Time, measurements and output are injected, so it runs on the host and has no dependency on Arduino.
class Supervisor {
public:
    explicit Supervisor(LineSink& out);

    void begin(uint32_t nowMs);  // prints the ready lines (via Sweep::begin)
    // One received character: load characters go to Prbs while it loads, otherwise to the command parser.
    void onChar(char c, uint32_t nowMs);
    void tick(uint32_t nowMs);  // every loop pass: load timeout, quiet window, duty of the current second
    // Once per measurement period, with the uncorrected results of both sensors.
    void onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient);

    uint16_t dutyCounts() const;  // the run's duty while a run is in progress, otherwise the sweep's
    // True exactly once after a run was accepted: the caller restarts the period scheduler and the averagers
    // so that second k of the run is measurement period k (plan D5).
    bool takeMeasurementRestart();

private:
    void dispatch(const Command& c, uint32_t nowMs);
    bool refuseWhileBusy();  // precedence (1)-(3) of plan D8 for `prbs load` and `prbs run`
    void emit(const char* text) { replies_.line(text); }

    LineSink& out_;
    PrefixSink replies_;  // command replies: the Sweep's and the Supervisor's own
    Sweep sweep_;
    Prbs prbs_;
    LineBuffer lineBuffer_;
    PeriodResult load_{0.0f, 0.0f, 0};
    PeriodResult ambient_{0.0f, 0.0f, 0};
    bool haveMeasurement_ = false;  // a period with samples on both sensors has been seen
    bool restart_ = false;
};
