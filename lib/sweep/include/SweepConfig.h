#pragma once
#include <stdint.h>

// Maximum duty the firmware may ever command, in % of the 12-bit range. THE ONE PLACE to change it:
// everything else (hard cap in counts, largest `start N`, last sweep step, table capacity and the
// printed messages) is derived from it. Set it for a build without editing code:
//   platformio.ini:  build_flags = -DSWEEP_CAP_PERCENT=40
//   or the environment: PLATFORMIO_BUILD_FLAGS="-DSWEEP_CAP_PERCENT=40" pio run -t upload
// Default 25 % (a 150 ohm resistor of the 3 in parallel dissipates about 0.24 W, just under 1/4 W).
#ifndef SWEEP_CAP_PERCENT
#define SWEEP_CAP_PERCENT 25
#endif
static_assert(SWEEP_CAP_PERCENT > 0 && SWEEP_CAP_PERCENT <= 100,
              "SWEEP_CAP_PERCENT must be in (0, 100]");

// Tunables of the duty sweep (plan D12). Change values here; the logic does not depend on them.
namespace sweepcfg {
constexpr float    kSettleThresholdC = 0.5f;  // max - min of the load temperature over the settle window
constexpr uint32_t kSettleWindowS    = 120;   // settle window, seconds
constexpr uint32_t kSettleBufferSize = 600;   // capacity of the per-second history (upper bound for the window)
constexpr uint32_t kStepTimeoutS     = 2700;  // 45 minutes: a step not settled by then is recorded as not settled
constexpr float    kStepPercent      = 2.5f;  // duty increment between steps
constexpr float    kCapPercent       = SWEEP_CAP_PERCENT;  // highest duty ever commanded (see above)
constexpr float    kCutoffC          = 80.0f; // uncorrected load temperature that forces duty 0 until reset
constexpr float    kStartToleranceC  = 3.0f;  // max |load - ambient| allowed to start a sweep
constexpr uint32_t kLogPeriodS       = 5;     // seconds between log lines while a sweep runs
constexpr uint32_t kStartupBoundS    = 5;     // startup must finish within this time
constexpr uint32_t kPwmFrequencyHz   = 1000;  // LEDC frequency (12-bit resolution)
constexpr float    kTargetsC[3]      = {55.0f, 60.0f, 65.0f};  // temperatures whose duty is reported
}  // namespace sweepcfg

static_assert(sweepcfg::kSettleWindowS <= sweepcfg::kSettleBufferSize,
              "settle window must fit in the history buffer");
