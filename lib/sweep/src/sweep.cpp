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

void Sweep::onPeriod(uint32_t, const PeriodResult& load, const PeriodResult& ambient) {
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
    }
}

void Sweep::handleStart(const Command& c) {
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
    char buf[48];
    snprintf(buf, sizeof buf, "START from %.1f %%", static_cast<double>(startPercent_));
    emit(buf);
}

void Sweep::onCommand(const Command& c, uint32_t) {
    char buf[128];
    switch (c.type) {
        case CmdType::None:
            break;
        case CmdType::Start:
            handleStart(c);
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
            emit("no results yet");
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
