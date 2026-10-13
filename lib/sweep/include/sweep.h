#pragma once
#include <stdint.h>

#include <stddef.h>

#include "SweepConfig.h"
#include "commands.h"
#include "measure.h"

// Sink for the text the firmware prints, one line per call (no trailing newline).
struct LineSink {
    virtual void line(const char* text) = 0;
    virtual ~LineSink() {}
};

enum class SweepState { Idle, HoldZero, Running, Done, Cutoff };

// One recorded duty step (loadC is already corrected by the sensor offset).
struct StepRow {
    float dutyPercent;
    uint16_t dutyCounts;
    float loadC;
    float ambientC;
    float loadCount;
    float ambientCount;
    bool settled;
};

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

    size_t rowCount() const { return rowCount_; }
    StepRow row(size_t i) const { return rows_[i]; }
    bool offsetKnown() const { return offsetKnown_; }
    float offsetC() const { return offsetC_; }

private:
    void emit(const char* text);
    void cutoff(const char* text);
    void handleStart(const Command& c, uint32_t nowMs);
    void beginStep(float percent, uint32_t nowMs);
    void closeStep(bool settled, uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient);
    void addRow(float percent, uint16_t counts, bool settled, const PeriodResult& load,
                const PeriodResult& ambient);

    LineSink& out_;
    SweepState state_ = SweepState::Idle;
    bool haveMeasurement_ = false;
    PeriodResult load_{0.0f, 0.0f, 0};
    PeriodResult ambient_{0.0f, 0.0f, 0};
    float startPercent_ = 0.0f;
    uint16_t duty_ = 0;

    // Step in progress: per-second (load, ambient) history, a ring of the last kHistory entries.
    static constexpr size_t kHistory = sweepcfg::kSettleBufferSize;
    static_assert(sweepcfg::kSettleWindowS <= sweepcfg::kSettleBufferSize,
                  "settle window must fit in the history ring");
    static constexpr size_t kMaxRows = 12;
    float loadHist_[kHistory];
    float ambHist_[kHistory];
    size_t histCount_ = 0;   // entries since the step began (not capped)
    uint32_t stepStartMs_ = 0;
    float stepPercent_ = 0.0f;

    StepRow rows_[kMaxRows];
    size_t rowCount_ = 0;
    bool offsetKnown_ = false;
    float offsetC_ = 0.0f;
};
