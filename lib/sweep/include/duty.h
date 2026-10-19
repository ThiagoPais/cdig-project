#pragma once
#include <stdint.h>

#include "SweepConfig.h"

// Duty is carried as a count of the 12-bit PWM range (0..4095 = 0..100 %).
constexpr uint16_t kFullScale = 4095;
// Hard cap in counts, derived from the single setting SWEEP_CAP_PERCENT (25 -> 1023, 40 -> 1638, 50 -> 2047).
constexpr uint16_t kCapCounts = static_cast<uint16_t>(sweepcfg::kCapPercent * kFullScale / 100.0f);

// Limits any count to the cap. The only place the cap is enforced.
uint16_t clampDuty(uint16_t counts);

// Rounds percent of full scale to the nearest count, then applies clampDuty.
uint16_t percentToCounts(float percent);

// counts / 4095 * 100.
float countsToPercent(uint16_t counts);
