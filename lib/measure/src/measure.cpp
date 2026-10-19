#include "measure.h"

namespace {
constexpr float kFullScaleMv = 1100.0f;
constexpr float kFullScaleCount = 4095.0f;
constexpr float kMvPerDegC = 10.0f;
constexpr uint32_t kPeriodMs = 1000;
constexpr uint32_t kWindowMs = 800;
constexpr uint32_t kSampleIntervalMs = 2;
}  // namespace

float countToCelsius(float count) {
    return count * (kFullScaleMv / kFullScaleCount) / kMvPerDegC;
}

void WindowAverager::reset() {
    if (n_ > 0) heldCount_ = static_cast<float>(sum_) / n_;
    sum_ = 0;
    n_ = 0;
}

void WindowAverager::add(uint16_t count) {
    sum_ += count;
    if (n_ < 0xFFFF) ++n_;
}

PeriodResult WindowAverager::result() const {
    PeriodResult r;
    r.samples = n_;
    r.meanCount = n_ > 0 ? static_cast<float>(sum_) / n_ : heldCount_;
    r.tempC = countToCelsius(r.meanCount);
    return r;
}

void PeriodScheduler::start(uint32_t nowMs) {
    if (!started_) {
        started_ = true;
        origin_ = nowMs;
    }
}

void PeriodScheduler::restart(uint32_t nowMs) {
    started_ = true;
    origin_ = nowMs;
    haveSample_ = false;
    reported_ = 0;
}

bool PeriodScheduler::takeSampleNow(uint32_t nowMs) {
    start(nowMs);
    const uint32_t phase = (nowMs - origin_) % kPeriodMs;
    if (phase >= kWindowMs) return false;
    if (haveSample_ && static_cast<uint32_t>(nowMs - lastSample_) < kSampleIntervalMs) return false;
    haveSample_ = true;
    lastSample_ = nowMs;
    return true;
}

bool PeriodScheduler::periodEnded(uint32_t nowMs) {
    start(nowMs);
    const uint32_t done = (nowMs - origin_) / kPeriodMs;
    if (done == reported_) return false;
    reported_ = done;
    return true;
}
