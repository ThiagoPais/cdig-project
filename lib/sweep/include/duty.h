#pragma once
#include <stdint.h>

// Duty is carried as a count of the 12-bit PWM range (0..4095 = 0..100 %).
constexpr uint16_t kFullScale = 4095;
constexpr uint16_t kCapCounts = 1023;  // 25 % of the range: the top of the first 1024 values

// Limits any count to the cap. The only place the cap is enforced.
uint16_t clampDuty(uint16_t counts);

// Rounds percent of full scale to the nearest count, then applies clampDuty.
uint16_t percentToCounts(float percent);

// counts / 4095 * 100.
float countsToPercent(uint16_t counts);
