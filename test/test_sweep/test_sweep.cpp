#include <unity.h>
#include <string>
#include <vector>

#include "sweep.h"
#include "commands.h"
#include "measure.h"

namespace {

struct RecSink : LineSink {
    std::vector<std::string> lines;
    void line(const char* text) override { lines.push_back(text); }
    bool anyHas(const char* s) const {
        for (const auto& l : lines)
            if (l.find(s) != std::string::npos) return true;
        return false;
    }
    bool oneLineHasAll(const char* a, const char* b) const {
        for (const auto& l : lines)
            if (l.find(a) != std::string::npos && l.find(b) != std::string::npos) return true;
        return false;
    }
};

PeriodResult pr(float t, float count = 941.2f, uint16_t n = 398) { return PeriodResult{t, count, n}; }

Command startCmd(float pct = 0.0f) { return Command{CmdType::Start, pct}; }
Command cmd(CmdType t) { return Command{t, 0.0f}; }

RecSink sink;
Sweep* sw = nullptr;

}  // namespace

void setUp() {
    sink.lines.clear();
    delete sw;
    sw = new Sweep(sink);
    sw->begin(0);
}
void tearDown() {}

// Moves to a measured, near-ambient state and starts.
static void startOk() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sw->onCommand(startCmd(), 1000);
    sink.lines.clear();
}

static void triggerCutoff() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sw->onPeriod(2000, pr(80.0f, 1800.0f), pr(25.0f));
}

void test_AC1_ready_message_lists_commands_and_settle_values() {
    TEST_ASSERT_TRUE(sink.anyHas("READY"));
    TEST_ASSERT_TRUE(sink.anyHas("start"));
    TEST_ASSERT_TRUE(sink.anyHas("stop"));
    TEST_ASSERT_TRUE(sink.anyHas("results"));
    TEST_ASSERT_TRUE(sink.anyHas("temp"));
    TEST_ASSERT_TRUE(sink.anyHas("0.5"));
    TEST_ASSERT_TRUE(sink.anyHas("120"));
}

void test_AC1_idle_with_duty_zero_after_begin() {
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC1_duty_stays_zero_in_idle_periods_until_start() {
    for (uint32_t i = 1; i <= 5; i++) sw->onPeriod(i * 1000, pr(25.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC2_start_accepted_when_within_tolerance() {
    sw->onPeriod(1000, pr(26.0f), pr(25.0f));
    sw->onCommand(startCmd(), 1000);
    TEST_ASSERT_TRUE(sink.anyHas("START"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC2_start_with_percent_accepted_and_duty_still_zero() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sw->onCommand(startCmd(10.0f), 1000);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC2_E3_start_accepted_at_exactly_3_0_difference() {
    sw->onPeriod(1000, pr(28.0f), pr(25.0f));
    sw->onCommand(startCmd(), 1000);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_AC2_E3_start_accepted_at_exactly_minus_3_0_difference() {
    sw->onPeriod(1000, pr(22.0f), pr(25.0f));
    sw->onCommand(startCmd(), 1000);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_E3_start_refused_when_load_too_far_above_ambient() {
    sw->onPeriod(1000, pr(28.5f), pr(25.0f));
    sink.lines.clear();
    sw->onCommand(startCmd(), 1000);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("REFUSED", "ambient"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E3_start_refused_when_load_too_far_below_ambient() {
    sw->onPeriod(1000, pr(21.5f), pr(25.0f));
    sink.lines.clear();
    sw->onCommand(startCmd(), 1000);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("REFUSED", "ambient"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E3_start_refused_without_any_measurement() {
    sw->onCommand(startCmd(), 100);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("REFUSED", "no measurement"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E1_start_ignored_while_running() {
    startOk();
    sw->onCommand(startCmd(5.0f), 1500);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("REFUSED", "already running"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E2_start_refused_after_cutoff() {
    triggerCutoff();
    sink.lines.clear();
    sw->onCommand(startCmd(), 2500);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("REFUSED", "reset"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E4_unknown_command_lists_valid_commands_state_unchanged() {
    sw->onCommand(cmd(CmdType::Unknown), 100);
    TEST_ASSERT_TRUE(sink.anyHas("UNKNOWN COMMAND"));
    TEST_ASSERT_TRUE(sink.anyHas("start"));
    TEST_ASSERT_TRUE(sink.anyHas("stop"));
    TEST_ASSERT_TRUE(sink.anyHas("results"));
    TEST_ASSERT_TRUE(sink.anyHas("temp"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E4_unknown_command_while_running_keeps_state() {
    startOk();
    sw->onCommand(cmd(CmdType::Unknown), 1500);
    TEST_ASSERT_TRUE(sink.anyHas("UNKNOWN COMMAND"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_E13_bad_start_refused_with_usage_state_unchanged() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::BadStart), 1100);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("REFUSED", "usage"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E13_bad_start_while_running_keeps_state() {
    startOk();
    sw->onCommand(cmd(CmdType::BadStart), 1500);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("REFUSED", "usage"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_AC13_stop_while_running_goes_idle_duty_zero_no_results() {
    startOk();
    sw->onCommand(cmd(CmdType::Stop), 1500);
    TEST_ASSERT_TRUE(sink.anyHas("STOPPED"));
    TEST_ASSERT_EQUAL_UINT32(1, sink.lines.size());
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC13_start_possible_again_after_stop() {
    startOk();
    sw->onCommand(cmd(CmdType::Stop), 1500);
    sw->onCommand(startCmd(), 1600);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_E11_stop_when_idle_is_noop_with_message() {
    sw->onCommand(cmd(CmdType::Stop), 100);
    TEST_ASSERT_TRUE(sink.anyHas("NOTHING RUNNING"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E11_stop_after_cutoff_is_noop_with_message() {
    triggerCutoff();
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Stop), 2500);
    TEST_ASSERT_TRUE(sink.anyHas("NOTHING RUNNING"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC8_cutoff_at_80_uncorrected_load() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sink.lines.clear();
    sw->onPeriod(2000, pr(80.0f, 1800.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "load"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC8_no_cutoff_just_below_80() {
    sw->onPeriod(1000, pr(79.9f, 1800.0f), pr(25.0f));
    TEST_ASSERT_FALSE(sink.anyHas("CUTOFF"));
    TEST_ASSERT_FALSE(sw->state() == SweepState::Cutoff);
}

void test_AC8_cutoff_while_running_zeroes_duty_and_latches() {
    startOk();
    sw->onPeriod(2000, pr(85.0f, 1900.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sink.anyHas("CUTOFF"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    // temperature back to normal does not release the latch
    for (uint32_t i = 3; i <= 6; i++) sw->onPeriod(i * 1000, pr(25.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    sw->onCommand(startCmd(), 7000);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC8_cutoff_in_first_period_without_prior_measurement() {
    sw->onPeriod(1000, pr(90.0f, 2000.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "load"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
}

void test_AC8_no_cutoff_when_load_has_zero_samples() {
    sw->onPeriod(1000, pr(90.0f, 2000.0f, 0), pr(25.0f));
    TEST_ASSERT_FALSE(sink.anyHas("CUTOFF"));
    TEST_ASSERT_FALSE(sw->state() == SweepState::Cutoff);
}

void test_AC9_cutoff_names_load_for_count_zero() {
    sw->onPeriod(1000, pr(0.0f, 0.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "load"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC9_cutoff_names_load_for_count_4095() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sink.lines.clear();
    sw->onPeriod(2000, pr(25.0f, 4095.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "load"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
}

void test_AC9_cutoff_names_ambient_for_count_zero() {
    sw->onPeriod(1000, pr(25.0f), pr(0.0f, 0.0f));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "ambient"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC9_cutoff_names_ambient_for_count_4095() {
    startOk();
    sw->onPeriod(2000, pr(25.0f), pr(25.0f, 4095.0f));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "ambient"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC9_no_cutoff_for_zero_count_with_zero_samples() {
    sw->onPeriod(1000, pr(25.0f), pr(0.0f, 0.0f, 0));
    TEST_ASSERT_FALSE(sink.anyHas("CUTOFF"));
}

void test_AC17_temp_prints_temperature_count_and_samples() {
    sw->onPeriod(1000, pr(25.3f, 941.2f, 398), pr(24.8f, 923.0f, 397));
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Temp), 1100);
    TEST_ASSERT_TRUE(sink.anyHas("TEMP"));
    TEST_ASSERT_TRUE(sink.anyHas("25.3"));
    TEST_ASSERT_TRUE(sink.anyHas("24.8"));
    TEST_ASSERT_TRUE(sink.anyHas("941.2"));
    TEST_ASSERT_TRUE(sink.anyHas("923.0"));
    TEST_ASSERT_TRUE(sink.anyHas("398"));
    TEST_ASSERT_TRUE(sink.anyHas("397"));
}

void test_AC17_temp_before_any_period_says_no_measurement() {
    sw->onCommand(cmd(CmdType::Temp), 100);
    TEST_ASSERT_TRUE(sink.oneLineHasAll("TEMP", "no measurement"));
}

void test_AC17_temp_works_while_running_without_state_change() {
    startOk();
    sw->onCommand(cmd(CmdType::Temp), 1500);
    TEST_ASSERT_TRUE(sink.anyHas("TEMP"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_AC17_temp_works_after_cutoff() {
    triggerCutoff();
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Temp), 2500);
    TEST_ASSERT_TRUE(sink.anyHas("TEMP"));
    TEST_ASSERT_TRUE(sink.anyHas("80.0"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_AC1_ready_message_lists_commands_and_settle_values);
    RUN_TEST(test_AC1_idle_with_duty_zero_after_begin);
    RUN_TEST(test_AC1_duty_stays_zero_in_idle_periods_until_start);
    RUN_TEST(test_AC2_start_accepted_when_within_tolerance);
    RUN_TEST(test_AC2_start_with_percent_accepted_and_duty_still_zero);
    RUN_TEST(test_AC2_E3_start_accepted_at_exactly_3_0_difference);
    RUN_TEST(test_AC2_E3_start_accepted_at_exactly_minus_3_0_difference);
    RUN_TEST(test_E3_start_refused_when_load_too_far_above_ambient);
    RUN_TEST(test_E3_start_refused_when_load_too_far_below_ambient);
    RUN_TEST(test_E3_start_refused_without_any_measurement);
    RUN_TEST(test_E1_start_ignored_while_running);
    RUN_TEST(test_E2_start_refused_after_cutoff);
    RUN_TEST(test_E4_unknown_command_lists_valid_commands_state_unchanged);
    RUN_TEST(test_E4_unknown_command_while_running_keeps_state);
    RUN_TEST(test_E13_bad_start_refused_with_usage_state_unchanged);
    RUN_TEST(test_E13_bad_start_while_running_keeps_state);
    RUN_TEST(test_AC13_stop_while_running_goes_idle_duty_zero_no_results);
    RUN_TEST(test_AC13_start_possible_again_after_stop);
    RUN_TEST(test_E11_stop_when_idle_is_noop_with_message);
    RUN_TEST(test_E11_stop_after_cutoff_is_noop_with_message);
    RUN_TEST(test_AC8_cutoff_at_80_uncorrected_load);
    RUN_TEST(test_AC8_no_cutoff_just_below_80);
    RUN_TEST(test_AC8_cutoff_while_running_zeroes_duty_and_latches);
    RUN_TEST(test_AC8_cutoff_in_first_period_without_prior_measurement);
    RUN_TEST(test_AC8_no_cutoff_when_load_has_zero_samples);
    RUN_TEST(test_AC9_cutoff_names_load_for_count_zero);
    RUN_TEST(test_AC9_cutoff_names_load_for_count_4095);
    RUN_TEST(test_AC9_cutoff_names_ambient_for_count_zero);
    RUN_TEST(test_AC9_cutoff_names_ambient_for_count_4095);
    RUN_TEST(test_AC9_no_cutoff_for_zero_count_with_zero_samples);
    RUN_TEST(test_AC17_temp_prints_temperature_count_and_samples);
    RUN_TEST(test_AC17_temp_before_any_period_says_no_measurement);
    RUN_TEST(test_AC17_temp_works_while_running_without_state_change);
    RUN_TEST(test_AC17_temp_works_after_cutoff);
    return UNITY_END();
}
