// Environment probe (plan D12): one line per second on the same serial path as the product firmware,
// used to validate A8 (output never delays the pace), A9 (port open/close does not reset), A12 (READY
// on reset) and A14 (clock accuracy). It does not touch the PWM pin.
#include <Arduino.h>
#include <esp_system.h>

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kPeriodMs = 1000;

uint32_t seq = 0;
uint32_t nextMs = 0;
uint32_t lastLoopUs = 0;
uint32_t maxGapUs = 0;
}  // namespace

void setup() {
    Serial.begin(kSerialBaud);
    Serial.printf("READY probe reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
    nextMs = millis() + kPeriodMs;
    lastLoopUs = micros();
}

void loop() {
    const uint32_t nowUs = micros();
    const uint32_t gap = nowUs - lastLoopUs;
    lastLoopUs = nowUs;
    if (gap > maxGapUs) maxGapUs = gap;

    const uint32_t nowMs = millis();
    if (static_cast<int32_t>(nowMs - nextMs) >= 0) {
        nextMs += kPeriodMs;
        Serial.printf("P,%lu,%lu,%lu\n", static_cast<unsigned long>(seq++), static_cast<unsigned long>(nowMs),
                      static_cast<unsigned long>(maxGapUs));
        maxGapUs = 0;
    }
}
