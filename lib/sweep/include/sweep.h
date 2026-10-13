#pragma once
#include <stdint.h>

#include "commands.h"
#include "measure.h"

// Sink for the text the firmware prints, one line per call (no trailing newline).
struct LineSink {
    virtual void line(const char* text) = 0;
    virtual ~LineSink() {}
};

enum class SweepState { Idle, HoldZero, Running, Done, Cutoff };

// The sweep state machine (plan D10). Time, measurements and output are injected, so it
// runs on the host and has no dependency on Arduino.
class Sweep {
public:
    explicit Sweep(LineSink& out);

    void begin(uint32_t nowMs);  // prints the ready message
    void onCommand(const Command& c, uint32_t nowMs);
    // Once per measurement period, with the uncorrected results of both sensors.
    void onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient);

    uint16_t dutyCounts() const;  // desired duty, already capped; 0 after a cutoff
    SweepState state() const { return state_; }

private:
    void emit(const char* text);
    void cutoff(const char* text);
    void handleStart(const Command& c);

    LineSink& out_;
    SweepState state_ = SweepState::Idle;
    bool haveMeasurement_ = false;
    PeriodResult load_{0.0f, 0.0f, 0};
    PeriodResult ambient_{0.0f, 0.0f, 0};
    float startPercent_ = 0.0f;
    uint16_t duty_ = 0;
};
