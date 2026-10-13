// Open-loop duty sweep firmware: hardware glue only (the logic lives in lib/measure and lib/sweep).
#include <Arduino.h>

#include "SweepConfig.h"
#include "commands.h"
#include "duty.h"
#include "measure.h"
#include "sweep.h"

namespace {

// Wiring (labels as printed on the DOIT-style DevKit board).
constexpr uint8_t kPinLoadSensor = 33;     // D33 = GPIO33, ADC1_CH5: LM35 on the load
constexpr uint8_t kPinAmbientSensor = 34;  // D34 = GPIO34, ADC1_CH6 (input only): LM35 for the ambient
constexpr uint8_t kPinPwm = 14;            // D14 = GPIO14: TIP120 base (PWM)

constexpr uint8_t kPwmChannel = 0;
constexpr uint8_t kPwmBits = 12;
constexpr uint32_t kSerialBaud = 115200;

struct SerialSink : LineSink {
    void line(const char* text) override { Serial.println(text); }
};

SerialSink sink;
Sweep sweep(sink);
LineBuffer lineBuffer;
PeriodScheduler scheduler;
WindowAverager loadAverager;
WindowAverager ambientAverager;
uint16_t writtenDuty = 0xFFFF;  // forces the first write

// The only place the duty reaches the pin (plan D5): clampDuty enforces the 25 % cap.
void writeDutyCounts(uint16_t counts) {
    ledcWrite(kPwmChannel, clampDuty(counts));
}

}  // namespace

void setup() {
    // Hold the load off as early as possible; the boot ROM may still glitch the pin (accepted).
    pinMode(kPinPwm, OUTPUT);
    digitalWrite(kPinPwm, LOW);

    Serial.begin(kSerialBaud);

    analogReadResolution(12);
    analogSetPinAttenuation(kPinLoadSensor, ADC_0db);
    analogSetPinAttenuation(kPinAmbientSensor, ADC_0db);

    ledcSetup(kPwmChannel, sweepcfg::kPwmFrequencyHz, kPwmBits);
    ledcAttachPin(kPinPwm, kPwmChannel);
    writeDutyCounts(0);

    sweep.begin(millis());
}

void loop() {
    const uint32_t now = millis();

    // Commands: one character at a time, never blocking.
    while (Serial.available() > 0) {
        Command cmd;
        if (lineBuffer.push(static_cast<char>(Serial.read()), &cmd)) {
            sweep.onCommand(cmd, now);
        }
    }

    // One result per sensor per period: hand it to the sweep, then open the next window.
    // Done before sampling so the first sample of a new period lands in the new window.
    if (scheduler.periodEnded(now)) {
        const PeriodResult load = loadAverager.result();
        const PeriodResult ambient = ambientAverager.result();
        loadAverager.reset();
        ambientAverager.reset();
        sweep.onPeriod(now, load, ambient);
    }

    // Sampling: every 2 ms inside the first 800 ms of each 1 s period (plan D4).
    if (scheduler.takeSampleNow(now)) {
        loadAverager.add(static_cast<uint16_t>(analogRead(kPinLoadSensor)));
        ambientAverager.add(static_cast<uint16_t>(analogRead(kPinAmbientSensor)));
    }

    const uint16_t duty = sweep.dutyCounts();
    if (duty != writtenDuty) {
        writeDutyCounts(duty);
        writtenDuty = duty;
    }
}
