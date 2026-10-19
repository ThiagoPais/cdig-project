#include "supervisor.h"

#include <stdio.h>

void PrefixSink::line(const char* text) {
    if (!prefix_) {
        out_.line(text);
        return;
    }
    char buf[160];
    snprintf(buf, sizeof buf, "# %s", text);  // longer text is truncated, never overflowed
    out_.line(buf);
}

// Run lines (`# RUN START`, data lines, `# RUN END`) go straight to `out`; only replies are prefixed.
Supervisor::Supervisor(LineSink& out) : out_(out), replies_(out), sweep_(replies_), prbs_(out) {}

void Supervisor::begin(uint32_t nowMs) { sweep_.begin(nowMs); }

void Supervisor::onChar(char c, uint32_t nowMs) {
    if (prbs_.loading()) {
        prbs_.onLoadChar(c, nowMs);
        return;
    }
    Command cmd;
    if (lineBuffer_.push(c, &cmd)) dispatch(cmd, nowMs);
}

void Supervisor::tick(uint32_t nowMs) { prbs_.tick(nowMs); }

bool Supervisor::refuseWhileBusy() {
    if (prbs_.running()) {
        emit("REFUSED: a PRBS run is in progress");
        return true;
    }
    if (sweep_.state() == SweepState::HoldZero || sweep_.state() == SweepState::Running) {
        emit("REFUSED: a duty sweep is in progress");
        return true;
    }
    if (sweep_.state() == SweepState::Cutoff || prbs_.latched()) {
        emit("REFUSED: reset required after cutoff");
        return true;
    }
    return false;
}

void Supervisor::dispatch(const Command& c, uint32_t nowMs) {
    replies_.setPrefix(prbs_.running());
    switch (c.type) {
        case CmdType::PrbsLoad:
            if (refuseWhileBusy()) return;
            prbs_.beginLoad(c.count, nowMs);
            return;
        case CmdType::PrbsRun:
            if (refuseWhileBusy()) return;
            if (prbs_.requestRun(nowMs, load_, ambient_, haveMeasurement_)) restart_ = true;
            return;
        case CmdType::Start:
            // A sweep in progress or a sweep cutoff is answered by the Sweep with its own text (NG8).
            if (prbs_.running()) {
                emit("REFUSED: a PRBS run is in progress");
                return;
            }
            if (prbs_.latched()) {
                emit("REFUSED: reset required after cutoff");
                return;
            }
            sweep_.onCommand(c, nowMs);
            return;
        case CmdType::Stop:
            if (prbs_.running()) {
                prbs_.stop(nowMs);
            } else {
                sweep_.onCommand(c, nowMs);
            }
            return;
        case CmdType::BadPrbs: {
            // The plan pins no text of its own for a malformed prbs line: answer like any unknown command.
            Command unknown = c;
            unknown.type = CmdType::Unknown;
            sweep_.onCommand(unknown, nowMs);
            return;
        }
        default:
            sweep_.onCommand(c, nowMs);
            return;
    }
}

void Supervisor::onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient) {
    load_ = load;
    ambient_ = ambient;
    if (load.samples > 0 && ambient.samples > 0) haveMeasurement_ = true;
    if (prbs_.running()) {
        // The run owns the heater: the sweep only remembers the values for `temp` (plan D7).
        sweep_.observe(load, ambient);
        prbs_.onPeriod(nowMs, load, ambient);
    } else {
        replies_.setPrefix(false);
        sweep_.onPeriod(nowMs, load, ambient);
    }
}

uint16_t Supervisor::dutyCounts() const {
    return prbs_.running() ? prbs_.dutyCounts() : sweep_.dutyCounts();
}

bool Supervisor::takeMeasurementRestart() {
    const bool pending = restart_;
    restart_ = false;
    return pending;
}
