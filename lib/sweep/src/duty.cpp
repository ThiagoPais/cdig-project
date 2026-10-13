#include "duty.h"

uint16_t clampDuty(uint16_t counts) {
    return counts > kCapCounts ? kCapCounts : counts;
}

uint16_t percentToCounts(float percent) {
    if (!(percent > 0.0f)) return 0;  // negative and NaN
    float counts = percent / 100.0f * kFullScale + 0.5f;
    if (counts > static_cast<float>(kFullScale)) counts = kFullScale;
    return clampDuty(static_cast<uint16_t>(counts));
}

float countsToPercent(uint16_t counts) {
    return static_cast<float>(counts) / kFullScale * 100.0f;
}
