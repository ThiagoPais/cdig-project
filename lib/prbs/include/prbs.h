#pragma once
#include <stdint.h>

#include "PrbsConfig.h"
#include "duty.h"
#include "measure.h"
#include "sweep.h"  // LineSink

enum class PrbsState { Idle, Loading, Confirming, Running, Latched };

// Open-loop PRBS unit (plan D2, D3, D5, D7, D9). Time, measurements and output are injected, so it runs on the
// host and has no dependency on Arduino. The two-level sequence is stored as one bit per value (plan D2).
class Prbs {
public:
    explicit Prbs(LineSink& out);

    // --- load (plan D3) ---
    // Announces a load of `count` values. Prints `PRBS LOAD READY n=<count>` and returns true, or prints a
    // `REFUSED:` line and returns false. Either way the stored sequence is discarded.
    bool beginLoad(uint32_t count, uint32_t nowMs);
    // One character of the load stream (only while loading()); '\n' ends a value line.
    void onLoadChar(char c, uint32_t nowMs);
    // Called every loop pass: load timeout, the quiet window after the last value, and the duty of the
    // current second while a run plays.
    void tick(uint32_t nowMs);

    // --- run (plan D5, D8, D9) ---
    // Checks (4)-(6) of D8: a stored sequence, a valid measurement and |load - ambient| within the start
    // tolerance. Prints `# RUN START ...` and the columns line and returns true, or prints `REFUSED: ...`.
    bool requestRun(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient, bool haveMeasurement);
    // Once per measurement period: prints the data line of every completed second not yet printed, and
    // `# RUN END ok` after the last one. Seconds are derived from nowMs only (plan D5).
    void onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient);

    // `stop`: ends a run in progress (duty 0 at once, `# RUN END aborted ... reason=stopped by user`, back to
    // Idle, no latch). Nothing happens when no run is in progress.
    void stop(uint32_t nowMs);

    bool running() const { return state_ == PrbsState::Running; }
    bool latched() const { return state_ == PrbsState::Latched; }  // set by a safety abort, cleared only by a reset
    uint16_t dutyCounts() const { return duty_; }  // already clamped; 0 unless a run is playing

    bool loading() const { return state_ == PrbsState::Loading || state_ == PrbsState::Confirming; }
    bool hasSequence() const { return hasSequence_; }
    PrbsState state() const { return state_; }

private:
    void emit(const char* text) { out_.line(text); }
    void refuse(const char* text);
    void endLine(uint32_t nowMs);
    uint16_t valueAt(uint32_t k) const;
    void abortRun(const char* reason, PrbsState next);

    LineSink& out_;
    PrbsState state_ = PrbsState::Idle;
    bool hasSequence_ = false;

    uint8_t bits_[prbscfg::kMaxValues / 8];
    uint16_t level_[2] = {0, 0};
    uint8_t levelsSeen_ = 0;
    uint32_t count_ = 0;     // announced number of values
    uint32_t received_ = 0;  // values accepted so far
    uint32_t changes_ = 0;
    uint16_t lastValue_ = 0;

    char line_[prbscfg::kLineMax];
    uint8_t lineLen_ = 0;
    bool lineOverflow_ = false;
    uint32_t lastEventMs_ = 0;  // beginLoad, or the last accepted value line
    uint32_t confirmMs_ = 0;    // when the last value was accepted

    // run
    uint32_t startMs_ = 0;
    uint32_t nextK_ = 0;  // first second without a data line
    uint16_t duty_ = 0;
    float lastLoadC_ = 0.0f;
    float lastAmbientC_ = 0.0f;
    bool haveLoad_ = false;
    bool haveAmbient_ = false;
};
