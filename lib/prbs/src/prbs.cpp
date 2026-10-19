#include "prbs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
bool isBlank(char c) { return c == ' ' || c == '\t'; }

// A window-mean count at either rail means a shorted or saturated sensor (same rule as the sweep).
bool onRail(const PeriodResult& r) { return r.meanCount <= 0.0f || r.meanCount >= 4095.0f; }
}  // namespace

Prbs::Prbs(LineSink& out) : out_(out) { memset(bits_, 0, sizeof bits_); }

void Prbs::refuse(const char* text) {
    hasSequence_ = false;
    state_ = PrbsState::Idle;
    emit(text);
}

bool Prbs::beginLoad(uint32_t count, uint32_t nowMs) {
    hasSequence_ = false;
    char buf[96];
    if (count == 0 || count > prbscfg::kMaxValues) {
        state_ = PrbsState::Idle;
        snprintf(buf, sizeof buf, "REFUSED: bad length %lu (must be 1..%lu)", static_cast<unsigned long>(count),
                 static_cast<unsigned long>(prbscfg::kMaxValues));
        emit(buf);
        return false;
    }
    memset(bits_, 0, sizeof bits_);
    level_[0] = level_[1] = 0;
    levelsSeen_ = 0;
    count_ = count;
    received_ = 0;
    changes_ = 0;
    lastValue_ = 0;
    lineLen_ = 0;
    lineOverflow_ = false;
    lastEventMs_ = nowMs;
    state_ = PrbsState::Loading;
    snprintf(buf, sizeof buf, "PRBS LOAD READY n=%lu", static_cast<unsigned long>(count));
    emit(buf);
    return true;
}

void Prbs::onLoadChar(char c, uint32_t nowMs) {
    char buf[96];
    if (state_ == PrbsState::Confirming) {
        // A character in the quiet window: the stream had more values than announced.
        snprintf(buf, sizeof buf, "REFUSED: more values than announced (%lu)", static_cast<unsigned long>(count_));
        refuse(buf);
        return;
    }
    if (state_ != PrbsState::Loading) return;
    if (c == '\r') return;
    if (c != '\n') {
        if (lineLen_ < prbscfg::kLineMax - 1) {
            line_[lineLen_++] = c;
        } else {
            lineOverflow_ = true;
        }
        return;
    }
    endLine(nowMs);
}

void Prbs::endLine(uint32_t nowMs) {
    char buf[96];
    const uint32_t index = received_ + 1;  // 1-based position in the stream
    const bool overflow = lineOverflow_;
    size_t begin = 0;
    size_t end = lineLen_;
    lineLen_ = 0;
    lineOverflow_ = false;
    while (begin < end && isBlank(line_[begin])) ++begin;
    while (end > begin && isBlank(line_[end - 1])) --end;

    bool digitsOnly = !overflow && end > begin;
    for (size_t i = begin; digitsOnly && i < end; ++i) {
        if (line_[i] < '0' || line_[i] > '9') digitsOnly = false;
    }
    if (!digitsOnly) {
        snprintf(buf, sizeof buf, "REFUSED: value %lu is not a non-negative integer", static_cast<unsigned long>(index));
        refuse(buf);
        return;
    }

    uint32_t value = 0;
    for (size_t i = begin; i < end; ++i) {
        value = value * 10 + static_cast<uint32_t>(line_[i] - '0');
        if (value > 65535) value = 65535;  // saturate: anything this large is above the cap anyway
    }
    if (end - begin > 5 || value > kCapCounts) {
        snprintf(buf, sizeof buf, "REFUSED: value %lu = %lu is above the cap %u", static_cast<unsigned long>(index),
                 static_cast<unsigned long>(value), static_cast<unsigned>(kCapCounts));
        refuse(buf);
        return;
    }

    const uint16_t v = static_cast<uint16_t>(value);
    uint8_t bit = 0;
    if (levelsSeen_ == 0) {
        level_[0] = level_[1] = v;
        levelsSeen_ = 1;
    } else if (v == level_[0]) {
        bit = 0;
    } else if (levelsSeen_ == 1) {
        level_[1] = v;
        levelsSeen_ = 2;
        bit = 1;
    } else if (v == level_[1]) {
        bit = 1;
    } else {
        snprintf(buf, sizeof buf, "REFUSED: more than two distinct values (value %lu)", static_cast<unsigned long>(index));
        refuse(buf);
        return;
    }

    if (received_ > 0 && v != lastValue_) ++changes_;
    lastValue_ = v;
    if (bit != 0) bits_[received_ / 8] = static_cast<uint8_t>(bits_[received_ / 8] | (1u << (received_ % 8)));
    ++received_;
    lastEventMs_ = nowMs;
    if (received_ == count_) {
        confirmMs_ = nowMs;
        state_ = PrbsState::Confirming;
    }
}

uint16_t Prbs::valueAt(uint32_t k) const {
    const uint8_t bit = static_cast<uint8_t>((bits_[k / 8] >> (k % 8)) & 1u);
    return clampDuty(level_[bit]);
}

bool Prbs::requestRun(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient,
                      bool haveMeasurement) {
    char buf[128];
    if (state_ == PrbsState::Latched) {
        emit("REFUSED: reset required after cutoff");
        return false;
    }
    if (state_ == PrbsState::Running) {
        emit("REFUSED: a PRBS run is in progress");
        return false;
    }
    if (!hasSequence_) {
        emit("REFUSED: no sequence loaded");
        return false;
    }
    if (!haveMeasurement) {
        emit("REFUSED: no measurement yet");
        return false;
    }
    const float diff = fabsf(load.tempC - ambient.tempC);
    if (diff > sweepcfg::kStartToleranceC) {
        snprintf(buf, sizeof buf,
                 "REFUSED: load must be at ambient temperature first (|load - ambient| = %.1f C > %.1f C)",
                 static_cast<double>(diff), static_cast<double>(sweepcfg::kStartToleranceC));
        emit(buf);
        return false;
    }
    state_ = PrbsState::Running;
    startMs_ = nowMs;
    nextK_ = 0;
    duty_ = valueAt(0);
    haveLoad_ = false;
    haveAmbient_ = false;
    snprintf(buf, sizeof buf, "# RUN START n=%lu cap=%u limit=%.1f", static_cast<unsigned long>(count_),
             static_cast<unsigned>(kCapCounts), static_cast<double>(prbscfg::kLimitC));
    emit(buf);
    emit("# columns: k,time_s,u_counts,load_C,ambient_C");
    return true;
}

void Prbs::abortRun(const char* reason, PrbsState next) {
    char buf[96];
    duty_ = 0;
    state_ = next;
    snprintf(buf, sizeof buf, "# RUN END aborted k=%lu reason=%s", static_cast<unsigned long>(nextK_), reason);
    emit(buf);
}

void Prbs::stop(uint32_t) {
    if (state_ == PrbsState::Running) abortRun("stopped by user", PrbsState::Idle);
}

void Prbs::onPeriod(uint32_t nowMs, const PeriodResult& load, const PeriodResult& ambient) {
    if (state_ != PrbsState::Running) return;
    // Safety first (plan D7): before any pending line is printed, only on results that hold samples.
    // The result belongs to the oldest unprinted second, whose data line is therefore not printed.
    if (load.samples > 0 && onRail(load)) {
        abortRun("sensor fault load", PrbsState::Latched);
        return;
    }
    if (ambient.samples > 0 && onRail(ambient)) {
        abortRun("sensor fault ambient", PrbsState::Latched);
        return;
    }
    if (load.samples > 0 && load.tempC >= prbscfg::kLimitC) {
        abortRun("over-temperature", PrbsState::Latched);
        return;
    }
    const uint32_t elapsed = static_cast<uint32_t>(nowMs - startMs_) / 1000u;
    const uint32_t completed = elapsed < count_ ? elapsed : count_;
    bool first = true;
    char buf[96];
    char loadText[16];
    char ambientText[16];
    while (nextK_ < completed) {
        // The fresh result belongs to the oldest pending second; later (catch-up) lines and results
        // without samples reuse the previous temperatures (E6).
        if (first && load.samples > 0) {
            lastLoadC_ = load.tempC;
            haveLoad_ = true;
        }
        if (first && ambient.samples > 0) {
            lastAmbientC_ = ambient.tempC;
            haveAmbient_ = true;
        }
        first = false;
        if (haveLoad_) {
            snprintf(loadText, sizeof loadText, "%.1f", static_cast<double>(lastLoadC_));
        } else {
            snprintf(loadText, sizeof loadText, "nan");
        }
        if (haveAmbient_) {
            snprintf(ambientText, sizeof ambientText, "%.1f", static_cast<double>(lastAmbientC_));
        } else {
            snprintf(ambientText, sizeof ambientText, "nan");
        }
        snprintf(buf, sizeof buf, "%lu,%lu,%u,%s,%s", static_cast<unsigned long>(nextK_),
                 static_cast<unsigned long>(elapsed), static_cast<unsigned>(valueAt(nextK_)), loadText,
                 ambientText);
        emit(buf);
        ++nextK_;
    }
    if (nextK_ >= count_) {
        snprintf(buf, sizeof buf, "# RUN END ok n=%lu", static_cast<unsigned long>(count_));
        emit(buf);
        state_ = PrbsState::Idle;
        duty_ = 0;
    }
}

void Prbs::tick(uint32_t nowMs) {
    char buf[96];
    if (state_ == PrbsState::Running) {
        const uint32_t elapsed = static_cast<uint32_t>(nowMs - startMs_) / 1000u;
        duty_ = elapsed < count_ ? valueAt(elapsed) : 0;
        return;
    }
    if (state_ == PrbsState::Loading) {
        if (static_cast<uint32_t>(nowMs - lastEventMs_) >= prbscfg::kLoadTimeoutMs) {
            snprintf(buf, sizeof buf, "REFUSED: no data for 5 s (got %lu of %lu values)",
                     static_cast<unsigned long>(received_), static_cast<unsigned long>(count_));
            refuse(buf);
        }
    } else if (state_ == PrbsState::Confirming) {
        if (static_cast<uint32_t>(nowMs - confirmMs_) >= prbscfg::kLoadTailMs) {
            const uint16_t low = level_[0] < level_[1] ? level_[0] : level_[1];
            const uint16_t high = level_[0] < level_[1] ? level_[1] : level_[0];
            snprintf(buf, sizeof buf, "PRBS LEVELS low=%u high=%u changes=%lu", static_cast<unsigned>(low),
                     static_cast<unsigned>(high), static_cast<unsigned long>(changes_));
            emit(buf);
            snprintf(buf, sizeof buf, "PRBS LOADED n=%lu", static_cast<unsigned long>(count_));
            emit(buf);
            hasSequence_ = true;
            state_ = PrbsState::Idle;
        }
    }
}
