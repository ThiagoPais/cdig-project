#pragma once
#include <stdint.h>

// Tunables of the duty sweep (plan D12). Change values here; the logic does not depend on them.
namespace sweepcfg {
constexpr float    kSettleThresholdC = 0.5f;  // max - min of the load temperature over the settle window
constexpr uint32_t kSettleWindowS    = 120;   // settle window, seconds
constexpr uint32_t kSettleBufferSize = 600;   // capacity of the per-second history (upper bound for the window)
constexpr uint32_t kStepTimeoutS     = 2700;  // 45 minutes: a step not settled by then is recorded as not settled
constexpr float    kStepPercent      = 2.5f;  // duty increment between steps
constexpr float    kCapPercent       = 25.0f; // highest duty ever commanded
constexpr float    kCutoffC          = 80.0f; // uncorrected load temperature that forces duty 0 until reset
constexpr float    kStartToleranceC  = 3.0f;  // max |load - ambient| allowed to start a sweep
constexpr uint32_t kLogPeriodS       = 5;     // seconds between log lines while a sweep runs
constexpr uint32_t kStartupBoundS    = 5;     // startup must finish within this time
constexpr uint32_t kPwmFrequencyHz   = 1000;  // LEDC frequency (12-bit resolution)
}  // namespace sweepcfg

static_assert(sweepcfg::kSettleWindowS <= sweepcfg::kSettleBufferSize,
              "settle window must fit in the history buffer");
