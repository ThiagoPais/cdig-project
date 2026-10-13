#include "sweep.h"

#include <math.h>
#include <stdio.h>

#include "SweepConfig.h"
#include "duty.h"

namespace {
constexpr float kRailLow = 0.0f;
constexpr float kRailHigh = 4095.0f;

bool onRail(const PeriodResult& r) {
    return r.samples > 0 && (r.meanCount <= kRailLow || r.meanCount >= kRailHigh);
}
}  // namespace

Sweep::Sweep(LineSink& out) : out_(out) {}

void Sweep::emit(const char* text) { out_.line(text); }

void Sweep::begin(uint32_t) {
    state_ = SweepState::Idle;
    duty_ = 0;
    char buf[96];
    emit("READY");
    emit("commands: start [N] (N = 0..25 %), stop, results, temp");
    snprintf(buf, sizeof buf, "settle: threshold=%.1f C, window=%u s",
             static_cast<double>(sweepcfg::kSettleThresholdC),
             static_cast<unsigned>(sweepcfg::kSettleWindowS));
    emit(buf);
}

uint16_t Sweep::dutyCounts() const {
    if (state_ == SweepState::Cutoff) return 0;
    return clampDuty(duty_);
}

void Sweep::cutoff(const char* text) {
    state_ = SweepState::Cutoff;
    duty_ = 0;
    emit(text);
}

void Sweep::onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient) {
    load_ = load;
    ambient_ = ambient;
    haveMeasurement_ = true;
    if (state_ == SweepState::Cutoff) return;

    char buf[96];
    if (onRail(load)) {
        snprintf(buf, sizeof buf, "CUTOFF load sensor reading invalid (count %.0f)",
                 static_cast<double>(load.meanCount));
        cutoff(buf);
        return;
    }
    if (onRail(ambient)) {
        snprintf(buf, sizeof buf, "CUTOFF ambient sensor reading invalid (count %.0f)",
                 static_cast<double>(ambient.meanCount));
        cutoff(buf);
        return;
    }
    if (load.samples > 0 && load.tempC >= sweepcfg::kCutoffC) {
        snprintf(buf, sizeof buf, "CUTOFF load temperature %.1f C >= %.1f C",
                 static_cast<double>(load.tempC), static_cast<double>(sweepcfg::kCutoffC));
        cutoff(buf);
        return;
    }

    if (state_ != SweepState::HoldZero && state_ != SweepState::Running) return;

    // A period with no samples carries held (stale) values: it must not feed the settle
    // history or the offset (plan D4). The wall-clock timeout still applies.
    const bool fresh = load.samples > 0 && ambient.samples > 0;
    if (fresh) {
        const size_t slot = histCount_ % kHistory;
        loadHist_[slot] = load.tempC;
        ambHist_[slot] = ambient.tempC;
        ++histCount_;
    }

    const size_t window = sweepcfg::kSettleWindowS;
    bool settled = false;
    if (fresh && histCount_ >= window) {
        float lo = loadHist_[(histCount_ - 1) % kHistory];
        float hi = lo;
        for (size_t k = 0; k < window; ++k) {
            const float v = loadHist_[(histCount_ - 1 - k) % kHistory];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        settled = (hi - lo) <= sweepcfg::kSettleThresholdC;
    }
    const bool timedOut =
        static_cast<uint32_t>(nowMs - stepStartMs_) >= sweepcfg::kStepTimeoutS * 1000u;
    if (settled || timedOut) closeStep(settled, nowMs, load, ambient);
}

void Sweep::addRow(float percent, uint16_t counts, bool settled, const PeriodResult& load,
                   const PeriodResult& ambient) {
    if (rowCount_ >= kMaxRows) return;
    StepRow& r = rows_[rowCount_++];
    r.dutyPercent = percent;
    r.dutyCounts = counts;
    r.loadC = load.tempC - offsetC_;
    r.ambientC = ambient.tempC;
    r.loadCount = load.meanCount;
    r.ambientCount = ambient.meanCount;
    r.settled = settled;
}

void Sweep::beginStep(float percent, uint32_t nowMs) {
    state_ = SweepState::Running;
    stepPercent_ = percent;
    duty_ = percentToCounts(percent);
    histCount_ = 0;
    stepStartMs_ = nowMs;
    char buf[64];
    snprintf(buf, sizeof buf, "STEP %.1f %% (%u counts)", static_cast<double>(percent),
             static_cast<unsigned>(duty_));
    emit(buf);
}

void Sweep::closeStep(bool settled, uint32_t nowMs, const PeriodResult& load,
                      const PeriodResult& ambient) {
    float next;
    if (state_ == SweepState::HoldZero) {
        // The hold ends: the offset is the mean (load - ambient) over the last window.
        const size_t n = histCount_ < sweepcfg::kSettleWindowS ? histCount_ : sweepcfg::kSettleWindowS;
        double sum = 0.0;
        for (size_t k = 0; k < n; ++k) {
            const size_t idx = (histCount_ - 1 - k) % kHistory;
            sum += static_cast<double>(loadHist_[idx]) - static_cast<double>(ambHist_[idx]);
        }
        offsetC_ = n > 0 ? static_cast<float>(sum / n) : 0.0f;
        offsetKnown_ = true;
        char buf[64];
        snprintf(buf, sizeof buf, "OFFSET %.2f C (load - ambient at 0 %%)", static_cast<double>(offsetC_));
        emit(buf);
        if (startPercent_ <= 0.0f) {
            addRow(0.0f, 0, settled, load, ambient);
            next = sweepcfg::kStepPercent;
        } else {
            next = startPercent_;
        }
    } else {
        addRow(stepPercent_, duty_, settled, load, ambient);
        next = stepPercent_ + sweepcfg::kStepPercent;
    }

    if (next > sweepcfg::kCapPercent + 1e-4f) {
        state_ = SweepState::Done;
        duty_ = 0;
        emit("SWEEP DONE");
        printTable(true);
        return;
    }
    beginStep(next, nowMs);
}

void Sweep::handleStart(const Command& c, uint32_t nowMs) {
    if (state_ == SweepState::Cutoff) {
        emit("REFUSED: reset required after cutoff");
        return;
    }
    if (state_ == SweepState::HoldZero || state_ == SweepState::Running) {
        emit("REFUSED: a sweep is already running");
        return;
    }
    if (!haveMeasurement_) {
        emit("REFUSED: no measurement yet");
        return;
    }
    const float diff = fabsf(load_.tempC - ambient_.tempC);
    if (diff > sweepcfg::kStartToleranceC) {
        char buf[128];
        snprintf(buf, sizeof buf,
                 "REFUSED: load must be at ambient temperature first (|load - ambient| = %.1f C > %.1f C)",
                 static_cast<double>(diff), static_cast<double>(sweepcfg::kStartToleranceC));
        emit(buf);
        return;
    }
    startPercent_ = c.startPercent;
    state_ = SweepState::HoldZero;
    duty_ = 0;
    rowCount_ = 0;
    offsetKnown_ = false;
    offsetC_ = 0.0f;
    histCount_ = 0;
    stepStartMs_ = nowMs;
    char buf[48];
    snprintf(buf, sizeof buf, "START from %.1f %%", static_cast<double>(startPercent_));
    emit(buf);
}

void Sweep::onCommand(const Command& c, uint32_t nowMs) {
    char buf[128];
    switch (c.type) {
        case CmdType::None:
            break;
        case CmdType::Start:
            handleStart(c, nowMs);
            break;
        case CmdType::BadStart:
            emit("REFUSED: usage: start | start N (N from 0 to 25)");
            break;
        case CmdType::Unknown:
            emit("UNKNOWN COMMAND: valid commands: start [N], stop, results, temp");
            break;
        case CmdType::Stop:
            if (state_ == SweepState::HoldZero || state_ == SweepState::Running) {
                state_ = SweepState::Idle;
                duty_ = 0;
                emit("STOPPED");
            } else {
                emit("NOTHING RUNNING");
            }
            break;
        case CmdType::Results:
            if (rowCount_ == 0) {
                emit("no results yet");
            } else {
                printTable(state_ == SweepState::Done);
            }
            break;
        case CmdType::Temp:
            if (!haveMeasurement_) {
                emit("TEMP no measurement yet");
            } else {
                snprintf(buf, sizeof buf,
                         "TEMP load=%.1f C count=%.1f n=%u ambient=%.1f C count=%.1f n=%u",
                         static_cast<double>(load_.tempC), static_cast<double>(load_.meanCount),
                         static_cast<unsigned>(load_.samples), static_cast<double>(ambient_.tempC),
                         static_cast<double>(ambient_.meanCount), static_cast<unsigned>(ambient_.samples));
                emit(buf);
            }
            break;
    }
}

// First adjacent pair of settled rows (ascending duty order, not-settled rows skipped) whose
// load temperatures bracket the target, ends included; linear interpolation of the duty.
bool Sweep::interpolateDuty(float targetC, float* percent) const {
    const StepRow* prev = nullptr;
    for (size_t i = 0; i < rowCount_; ++i) {
        const StepRow& cur = rows_[i];
        if (!cur.settled) continue;
        if (prev != nullptr) {
            const double a = prev->loadC;
            const double b = cur.loadC;
            const double lo = a < b ? a : b;
            const double hi = a < b ? b : a;
            if (targetC >= lo && targetC <= hi) {
                const double da = countsToPercent(prev->dutyCounts);
                const double db = countsToPercent(cur.dutyCounts);
                *percent = static_cast<float>(b == a ? da : da + (targetC - a) * (db - da) / (b - a));
                return true;
            }
        }
        prev = &cur;
    }
    return false;
}

void Sweep::printTable(bool withTargets) {
    char buf[128];
    snprintf(buf, sizeof buf, "TABLE offset=%.2f C", static_cast<double>(offsetC_));
    emit(buf);
    emit("duty_pct duty_count load_C ambient_C load_adc ambient_adc settled");
    for (size_t i = 0; i < rowCount_; ++i) {
        const StepRow& r = rows_[i];
        snprintf(buf, sizeof buf, "%.2f %u %.1f %.1f %.1f %.1f %s",
                 static_cast<double>(countsToPercent(r.dutyCounts)), static_cast<unsigned>(r.dutyCounts),
                 static_cast<double>(r.loadC), static_cast<double>(r.ambientC),
                 static_cast<double>(r.loadCount), static_cast<double>(r.ambientCount),
                 r.settled ? "yes" : "no");
        emit(buf);
    }
    if (!withTargets) return;
    for (float target : sweepcfg::kTargetsC) {
        float percent = 0.0f;
        if (interpolateDuty(target, &percent)) {
            snprintf(buf, sizeof buf, "TARGET %.1f C: duty=%.1f %% (%u counts)", static_cast<double>(target),
                     static_cast<double>(percent), static_cast<unsigned>(percentToCounts(percent)));
        } else {
            snprintf(buf, sizeof buf, "TARGET %.1f C: not reachable within the table",
                     static_cast<double>(target));
        }
        emit(buf);
    }
}
