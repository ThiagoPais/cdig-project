#pragma once
#include <stdint.h>

// Noise-averaged temperature measurement, independent of the sweep and of Arduino.
// One value per sensor per 1 s period, taken as the mean of the samples of the first 0.8 s.

struct PeriodResult {
    float tempC;       // mean count converted to degrees C (uncorrected)
    float meanCount;   // mean raw 12-bit ADC count of the window
    uint16_t samples;  // number of samples in the window (0 = nothing was collected)
};

// Ideal conversion: 1.1 V full scale over 4095 counts, 10 mV per degree C.
float countToCelsius(float count);

class WindowAverager {
public:
    void reset();                    // closes the window: keeps the last result as the held value
    void add(uint16_t count);
    PeriodResult result() const;     // mean of the open window; with no samples: held values, samples = 0
private:
    uint32_t sum_ = 0;
    uint16_t n_ = 0;
    float heldCount_ = 0.0f;
};

// Decides when to sample (every 2 ms inside the first 800 ms of each 1000 ms period)
// and when a period has ended. The origin is the time of the first call.
class PeriodScheduler {
public:
    bool takeSampleNow(uint32_t nowMs);
    bool periodEnded(uint32_t nowMs);
    // Re-anchors the scheduler: the origin becomes nowMs, the period counter returns to 0 and the sample
    // spacing is reset (a PRBS run makes second k of the run exactly period k).
    void restart(uint32_t nowMs);
private:
    void start(uint32_t nowMs);
    bool started_ = false;
    uint32_t origin_ = 0;
    uint32_t lastSample_ = 0;
    bool haveSample_ = false;
    uint32_t reported_ = 0;
};
