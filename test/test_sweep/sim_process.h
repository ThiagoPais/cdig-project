#pragma once
// Simulated first-order thermal process with injected time (no wall clock).
//   dT/dt = (Ta + K * dutyFraction - T) / tau
// Integrated with explicit Euler, one step per simulated second.
// The caller feeds the duty counts the firmware applied during the last period.
#include <stdint.h>

struct FirstOrderProcess {
    float ambientC;       // Ta
    float gainC;          // K: rise at 100 % duty (degrees C)
    float tauS;           // time constant (s)
    float tempC;          // T, starts at ambient
    float fullScaleCounts;

    FirstOrderProcess(float ta = 25.0f, float k = 100.0f, float tau = 60.0f)
        : ambientC(ta), gainC(k), tauS(tau), tempC(ta), fullScaleCounts(4095.0f) {}

    // Advance by dtS seconds with the given applied duty counts; returns the new T.
    float step(uint16_t dutyCounts, float dtS = 1.0f) {
        const float frac = static_cast<float>(dutyCounts) / fullScaleCounts;
        const float eq = ambientC + gainC * frac;
        tempC += (eq - tempC) * dtS / tauS;
        return tempC;
    }

    float equilibrium(uint16_t dutyCounts) const {
        return ambientC + gainC * (static_cast<float>(dutyCounts) / fullScaleCounts);
    }
};
