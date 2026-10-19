#include <unity.h>
#include <string>
#include <string.h>
#include <vector>

#include "sweep.h"
#include "commands.h"
#include "measure.h"
#include "sim_process.h"
#include "duty.h"
#include <stdio.h>

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


// ---------------------------------------------------------------------------
// T6: 0 % hold, steps, settle detection, offset
// ---------------------------------------------------------------------------

static uint32_t g_now = 0;

// Idle -> measured near-ambient -> start (percent). Injected clock is left at 1000 ms.
static void startAt(float pct) {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sw->onCommand(startCmd(pct), 1000);
    g_now = 1000;
    sink.lines.clear();
}

// One period of one second.
static void tick(float load, float amb, float loadCount = 941.2f, float ambCount = 923.0f) {
    g_now += 1000;
    sw->onPeriod(g_now, pr(load, loadCount), pr(amb, ambCount));
}

static void tickN(int n, float load, float amb) {
    for (int i = 0; i < n; i++) tick(load, amb);
}

static int countLinesWith(const char* s) {
    int n = 0;
    for (const auto& l : sink.lines)
        if (l.find(s) != std::string::npos) n++;
    return n;
}

void test_AC3_hold_starts_at_zero_with_no_rows_or_offset() {
    startAt(0.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_FALSE(sw->offsetKnown());
}

void test_AC3_constant_signal_settles_exactly_at_120_periods() {
    startAt(0.0f);
    tickN(119, 25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_FALSE(sw->offsetKnown());
    tick(25.0f, 25.0f);  // 120th entry
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_TRUE(sw->row(0).settled);
}

void test_AC3_range_of_exactly_threshold_settles() {
    startAt(0.0f);
    for (int i = 0; i < 120; i++) tick((i % 2) ? 25.5f : 25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_TRUE(sw->row(0).settled);
}

void test_AC3_range_above_threshold_does_not_settle() {
    startAt(0.0f);
    for (int i = 0; i < 300; i++) tick((i % 2) ? 25.5625f : 25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_FALSE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC3_only_last_120_entries_count_for_range_and_offset() {
    startAt(0.0f);
    tickN(10, 26.0f, 25.0f);   // wide early values
    tickN(119, 25.0f, 25.0f);  // window still contains one 26.0
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_FALSE(sw->offsetKnown());
    tick(25.0f, 25.0f);        // window now all 25.0
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sw->offsetC());
}

void test_AC15_offset_is_mean_of_load_minus_ambient_and_printed_once() {
    startAt(0.0f);
    tickN(120, 25.75f, 25.0f);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, sw->offsetC());
    TEST_ASSERT_TRUE(sink.oneLineHasAll("OFFSET", "0.75"));
    TEST_ASSERT_EQUAL_INT(1, countLinesWith("OFFSET"));
    // later periods do not print or change it
    tickN(10, 25.75f, 26.0f);
    TEST_ASSERT_EQUAL_INT(1, countLinesWith("OFFSET"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, sw->offsetC());
}

void test_AC15_offset_uses_mean_not_last_value() {
    startAt(0.0f);
    for (int i = 0; i < 120; i++) tick((i % 2) ? 25.5f : 25.0f, 25.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, sw->offsetC());
}

void test_AC15_start_zero_records_hold_as_row_zero_with_corrected_load() {
    startAt(0.0f);
    tickN(120, 25.75f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    StepRow r = sw->row(0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.dutyPercent);
    TEST_ASSERT_EQUAL_UINT16(0, r.dutyCounts);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.loadC);  // 25.75 - 0.75
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.ambientC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 941.2f, r.loadCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 923.0f, r.ambientCount);
    TEST_ASSERT_TRUE(r.settled);
}

void test_AC15_start_n_does_not_record_hold_as_row() {
    startAt(10.0f);
    tickN(120, 25.5f, 25.0f);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_TRUE(sink.oneLineHasAll("STEP", "10"));
}

void test_AC15_offset_not_applied_to_cutoff_check() {
    startAt(0.0f);
    tickN(120, 28.0f, 25.0f);  // offset +3.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, sw->offsetC());
    tick(80.0f, 25.0f);        // corrected would be 77.0, uncorrected 80.0
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC2_steps_run_ascending_from_zero_with_correct_counts() {
    startAt(0.0f);
    tickN(120, 25.0f, 25.0f);  // hold settles -> step 2.5 begins
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_EQUAL_UINT16(102, sw->dutyCounts());
    TEST_ASSERT_TRUE(sink.oneLineHasAll("STEP", "2.5"));
    tickN(120, 25.0f, 25.0f);  // step 2.5 settles -> step 5.0
    TEST_ASSERT_EQUAL_UINT32(2, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT16(205, sw->dutyCounts());
    TEST_ASSERT_TRUE(sink.oneLineHasAll("STEP", "5.0"));
    tickN(120, 25.0f, 25.0f);  // -> 7.5
    TEST_ASSERT_EQUAL_UINT16(307, sw->dutyCounts());
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
}

void test_AC2_E8_full_constant_sweep_has_eleven_rows_ending_at_1023() {
    startAt(0.0f);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) {
        TEST_ASSERT_TRUE(sw->dutyCounts() <= 1023);
        tick(25.0f, 25.0f);
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_INT(11 * 120, guard);
    TEST_ASSERT_EQUAL_UINT32(11, sw->rowCount());
    for (size_t i = 0; i < 11; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f * (float)i, sw->row(i).dutyPercent);
        TEST_ASSERT_TRUE(sw->row(i).settled);
        if (i > 0) TEST_ASSERT_TRUE(sw->row(i).dutyCounts > sw->row(i - 1).dutyCounts);
    }
    TEST_ASSERT_EQUAL_UINT16(0, sw->row(0).dutyCounts);
    TEST_ASSERT_EQUAL_UINT16(102, sw->row(1).dutyCounts);
    TEST_ASSERT_EQUAL_UINT16(205, sw->row(2).dutyCounts);
    TEST_ASSERT_EQUAL_UINT16(1023, sw->row(10).dutyCounts);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());  // immediately at the end
    TEST_ASSERT_TRUE(sink.anyHas("SWEEP DONE"));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("STEP", "25"));
}

void test_E8_last_step_is_applied_as_1023_counts_while_running() {
    startAt(0.0f);
    tickN(120 * 10, 25.0f, 25.0f);  // hold + steps 2.5..22.5 closed
    TEST_ASSERT_EQUAL_UINT32(10, sw->rowCount());
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_EQUAL_UINT16(1023, sw->dutyCounts());
}

void test_AC2_start_n_runs_n_plus_k_times_2_5_up_to_25() {
    startAt(10.0f);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) tick(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT32(7, sw->rowCount());  // 10, 12.5, ..., 25
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, sw->row(0).dutyPercent);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, sw->row(6).dutyPercent);
    TEST_ASSERT_EQUAL_UINT16(512, sw->row(1).dutyCounts);  // 12.5 %
    TEST_ASSERT_EQUAL_UINT16(1023, sw->row(6).dutyCounts);
}

void test_AC2_start_n_not_multiple_does_not_force_last_step_to_25() {
    startAt(7.0f);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) tick(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT32(8, sw->rowCount());  // 7, 9.5, ..., 24.5
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.0f, sw->row(0).dutyPercent);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.5f, sw->row(7).dutyPercent);
    TEST_ASSERT_TRUE(sw->row(7).dutyCounts < 1023);
}

void test_AC4_step_not_settled_at_exactly_45_minutes() {
    startAt(0.0f);
    // ramp: range over any 120 s window is ~1.2 degrees, never settles
    for (int k = 1; k <= 2699; k++) tick(25.0f + 0.01f * (float)k, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_FALSE(sw->offsetKnown());
    tick(25.0f + 0.01f * 2700.0f, 25.0f);  // 2700 s elapsed
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_FALSE(sw->row(0).settled);
    // offset = mean over the last 120 entries (k = 2581..2700)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.405f, sw->offsetC());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 52.0f - 26.405f, sw->row(0).loadC);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, sw->row(0).ambientC);
    TEST_ASSERT_EQUAL_UINT16(102, sw->dutyCounts());  // sweep continues
}

void test_AC4_every_step_times_out_after_its_own_45_minutes() {
    startAt(0.0f);
    for (int t = 1; t <= 11 * 2700; t++) {
        tick((t % 2) ? 25.0f : 27.0f, 25.0f);  // range 2.0, never settles
        TEST_ASSERT_EQUAL_UINT32((size_t)(t / 2700), sw->rowCount());
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    for (size_t i = 0; i < 11; i++) TEST_ASSERT_FALSE(sw->row(i).settled);
    TEST_ASSERT_TRUE(sink.anyHas("SWEEP DONE"));
}

void test_AC4_settling_is_checked_before_timeout() {
    startAt(0.0f);
    // wide for 2579 s, then constant: window becomes flat at exactly 2699 s.
    for (int k = 1; k <= 2579; k++) tick((k % 2) ? 25.0f : 27.0f, 25.0f);
    tickN(120, 25.0f, 25.0f);  // t = 2699 s, flat window
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_TRUE(sw->row(0).settled);
}

void test_E7_cutoff_in_middle_of_window_records_nothing() {
    startAt(0.0f);
    tickN(60, 25.0f, 25.0f);
    tick(80.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_FALSE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    tickN(200, 25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
}

void test_E7_cutoff_on_the_period_that_would_settle_wins() {
    startAt(0.0f);
    tickN(119, 25.0f, 25.0f);
    g_now += 1000;
    sw->onPeriod(g_now, pr(25.0f), pr(25.0f, 4095.0f));  // would be the 120th, flat
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "ambient"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_FALSE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_E7_cutoff_in_later_step_keeps_earlier_rows_only() {
    startAt(0.0f);
    tickN(120, 25.0f, 25.0f);   // row 0
    tickN(119, 25.0f, 25.0f);   // step 2.5 almost settled
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    tick(80.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    tickN(300, 25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
}

void test_E6_each_row_keeps_its_own_ambient_and_offset_is_not_recomputed() {
    startAt(0.0f);
    // ambient = 25 + 0.25 * rowCount, constant within each step; load constant 25.5
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) {
        float amb = 25.0f + 0.25f * (float)sw->rowCount();
        float lc = 900.0f + (float)sw->rowCount();
        float ac = 800.0f + (float)sw->rowCount();
        tick(25.5f, amb, lc, ac);
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT32(11, sw->rowCount());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, sw->offsetC());
    TEST_ASSERT_EQUAL_INT(1, countLinesWith("OFFSET"));
    for (size_t i = 0; i < 11; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f + 0.25f * (float)i, sw->row(i).ambientC);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, sw->row(i).loadC);  // 25.5 - 0.5
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 900.0f + (float)i, sw->row(i).loadCount);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 800.0f + (float)i, sw->row(i).ambientCount);
    }
}

void test_AC2_AC3_AC15_simulated_first_order_process_full_sweep() {
    FirstOrderProcess proc(25.0f, 100.0f, 60.0f);
    const float sensorBias = 0.4f;  // load sensor reads high by 0.4 degrees C
    sw->onPeriod(1000, pr(proc.tempC + sensorBias), pr(25.0f));
    sw->onCommand(startCmd(0.0f), 1000);
    g_now = 1000;
    sink.lines.clear();

    int ticks = 0;
    while (sw->state() != SweepState::Done && sw->state() != SweepState::Cutoff && ticks < 20000) {
        proc.step(sw->dutyCounts());
        tick(proc.tempC + sensorBias, 25.0f);
        ticks++;
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_TRUE(ticks >= 11 * 120);
    TEST_ASSERT_EQUAL_UINT32(11, sw->rowCount());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sensorBias, sw->offsetC());
    for (size_t i = 0; i < 11; i++) {
        StepRow r = sw->row(i);
        TEST_ASSERT_TRUE(r.settled);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.ambientC);
        float eq = proc.equilibrium(r.dutyCounts);
        TEST_ASSERT_TRUE(r.loadC <= eq + 0.01f);
        TEST_ASSERT_TRUE(r.loadC >= eq - 1.2f);
        if (i > 0) TEST_ASSERT_TRUE(r.loadC > sw->row(i - 1).loadC);
    }
    TEST_ASSERT_EQUAL_UINT16(1023, sw->row(10).dutyCounts);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC2_stop_mid_sweep_then_new_start_discards_old_rows_and_offset() {
    startAt(0.0f);
    tickN(240, 25.75f, 25.0f);  // hold + step 2.5 recorded
    TEST_ASSERT_EQUAL_UINT32(2, sw->rowCount());
    sw->onCommand(cmd(CmdType::Stop), g_now);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    tick(25.0f, 25.0f);
    sw->onCommand(startCmd(5.0f), g_now);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_FALSE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    // history is fresh: needs a full new window of 120 entries
    tickN(119, 25.0f, 25.0f);
    TEST_ASSERT_FALSE(sw->offsetKnown());
    tick(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sw->offsetC());
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());  // start 5: hold is not a row
}

// ---------------------------------------------------------------------------
// T6 fix round 1: periods with zero samples (loop stalled), plan.md D4
// ---------------------------------------------------------------------------

// A period that collected no samples: held (stale) values, samples = 0.
static void tickStale(float load, float amb, float loadCount = 941.2f, float ambCount = 923.0f) {
    g_now += 1000;
    sw->onPeriod(g_now, pr(load, loadCount, 0), pr(amb, ambCount, 0));
}

void test_D4_AC3_zero_sample_periods_do_not_count_toward_settle_window() {
    startAt(0.0f);
    tickN(119, 25.0f, 25.0f);
    for (int i = 0; i < 50; i++) tickStale(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_FALSE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    tick(25.0f, 25.0f);  // the 120th real entry
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_TRUE(sw->row(0).settled);
}

void test_D4_AC3_zero_sample_ambient_periods_do_not_count_toward_settle_window() {
    startAt(0.0f);
    tickN(119, 25.0f, 25.0f);
    for (int i = 0; i < 10; i++) {
        g_now += 1000;
        sw->onPeriod(g_now, pr(25.0f), pr(25.0f, 923.0f, 0));  // only ambient stalled
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_FALSE(sw->offsetKnown());
    tick(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
}

void test_D4_AC15_stale_values_do_not_enter_offset_or_range() {
    startAt(0.0f);
    for (int i = 0; i < 120; i++) {
        tick(25.75f, 25.0f);          // real: load - ambient = 0.75
        tickStale(30.0f, 25.0f);      // stale: load - ambient = 5.0
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, sw->offsetC());
    TEST_ASSERT_TRUE(sink.oneLineHasAll("OFFSET", "0.75"));
    TEST_ASSERT_EQUAL_INT(1, countLinesWith("OFFSET"));
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_TRUE(sw->row(0).settled);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, sw->row(0).loadC);
}

void test_D4_AC8_zero_sample_periods_with_hot_held_value_do_not_cut_off() {
    startAt(0.0f);
    tickN(10, 25.0f, 25.0f);
    for (int i = 0; i < 5; i++) tickStale(85.0f, 25.0f, 1900.0f);
    TEST_ASSERT_FALSE(sink.anyHas("CUTOFF"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    tick(85.0f, 25.0f, 1900.0f);  // a real period at 85 C
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "load"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_D4_AC9_zero_sample_periods_with_rail_held_counts_do_not_cut_off() {
    startAt(0.0f);
    tickN(10, 25.0f, 25.0f);
    tickStale(25.0f, 25.0f, 0.0f, 923.0f);
    tickStale(25.0f, 25.0f, 941.2f, 4095.0f);
    TEST_ASSERT_FALSE(sink.anyHas("CUTOFF"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_D4_AC4_only_zero_sample_periods_still_time_out_at_2700_s() {
    startAt(0.0f);
    for (int k = 1; k <= 2699; k++) tickStale(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    tickStale(25.0f, 25.0f);  // 2700 s since the step began
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_FALSE(sw->row(0).settled);
    TEST_ASSERT_EQUAL_UINT16(102, sw->dutyCounts());
}


// ---------------------------------------------------------------------------
// T7: result table, interpolation, results command
// ---------------------------------------------------------------------------

static const char* kColumnLine = "duty_pct duty_count load_C ambient_C load_adc ambient_adc settled";
static const size_t kNpos = static_cast<size_t>(-1);

static size_t findPrefix(const char* prefix, size_t from = 0) {
    size_t n = strlen(prefix);
    for (size_t i = from; i < sink.lines.size(); i++)
        if (sink.lines[i].compare(0, n, prefix) == 0) return i;
    return kNpos;
}

static std::string expectedRowLine(size_t i) {
    StepRow r = sw->row(i);
    char buf[160];
    snprintf(buf, sizeof buf, "%.2f %u %.1f %.1f %.1f %.1f %s", (double)countsToPercent(r.dutyCounts),
             (unsigned)r.dutyCounts, (double)r.loadC, (double)r.ambientC, (double)r.loadCount,
             (double)r.ambientCount, r.settled ? "yes" : "no");
    return std::string(buf);
}

// Reference of the interpolation rule fixed for T7.
static bool refInterp(float target, float* dutyPct) {
    bool have = false;
    StepRow prev{};
    for (size_t i = 0; i < sw->rowCount(); i++) {
        StepRow r = sw->row(i);
        if (!r.settled) continue;
        if (have) {
            float lo = prev.loadC < r.loadC ? prev.loadC : r.loadC;
            float hi = prev.loadC < r.loadC ? r.loadC : prev.loadC;
            if (lo <= target && target <= hi) {
                float da = countsToPercent(prev.dutyCounts), db = countsToPercent(r.dutyCounts);
                *dutyPct = (r.loadC == prev.loadC) ? da : da + (target - prev.loadC) * (db - da) / (r.loadC - prev.loadC);
                return true;
            }
        }
        prev = r;
        have = true;
    }
    return false;
}

// Asserts the table starting at index `head` (the TABLE line) lists every recorded row, in order.
static void expectTableAt(size_t head, size_t* nextIdx) {
    TEST_ASSERT_TRUE(head != kNpos);
    TEST_ASSERT_TRUE(head + 1 + sw->rowCount() < sink.lines.size() + 1);
    TEST_ASSERT_TRUE(sink.lines[head].find("offset=") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING(kColumnLine, sink.lines[head + 1].c_str());
    for (size_t i = 0; i < sw->rowCount(); i++) {
        TEST_ASSERT_TRUE(head + 2 + i < sink.lines.size());
        TEST_ASSERT_EQUAL_STRING(expectedRowLine(i).c_str(), sink.lines[head + 2 + i].c_str());
    }
    *nextIdx = head + 2 + sw->rowCount();
}

static const float kTargets[3] = {55.0f, 60.0f, 65.0f};

// Parses TARGET line at idx; returns true when reachable.
static bool parseTarget(size_t idx, float target, float* duty, unsigned* counts) {
    TEST_ASSERT_TRUE(idx < sink.lines.size());
    char head[32];
    snprintf(head, sizeof head, "TARGET %.1f C:", (double)target);
    const std::string& l = sink.lines[idx];
    TEST_ASSERT_TRUE_MESSAGE(l.compare(0, strlen(head), head) == 0, l.c_str());
    if (l.find("not reachable within the table") != std::string::npos) return false;
    float d = -1.0f;
    unsigned c = 0;
    char fmt[64];
    snprintf(fmt, sizeof fmt, "TARGET %.1f C: duty=%%f %%%% (%%u counts)", (double)target);
    int n = sscanf(l.c_str(), fmt, &d, &c);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, l.c_str());
    *duty = d;
    *counts = c;
    return true;
}

// Checks the three TARGET lines at idx against the reference rule.
static void expectTargetsMatchReference(size_t idx) {
    for (int k = 0; k < 3; k++) {
        float want = 0.0f;
        bool reach = refInterp(kTargets[k], &want);
        float got = 0.0f;
        unsigned cnt = 0;
        bool gotReach = parseTarget(idx + k, kTargets[k], &got, &cnt);
        TEST_ASSERT_EQUAL_INT(reach ? 1 : 0, gotReach ? 1 : 0);
        if (reach) {
            TEST_ASSERT_FLOAT_WITHIN(0.06f, want, got);
            TEST_ASSERT_TRUE((int)cnt >= (int)percentToCounts(want) - 3);
            TEST_ASSERT_TRUE((int)cnt <= (int)percentToCounts(want) + 3);
        }
    }
}

// Runs a sweep with fixed per-row loads (ambient 25, hold load = holdLoad). noisy[i] rows alternate
// load / load+2 so they never settle (and time out). startPct must be > 0 so the hold is not a row.
static void runRows(float startPct, const float* loads, const bool* noisy, size_t nRows, float holdLoad = 25.0f) {
    startAt(startPct);
    int guard = 0, par = 0;
    while (sw->state() != SweepState::Done && sw->state() != SweepState::Cutoff && guard++ < 11 * 2800) {
        if (sw->state() == SweepState::HoldZero || sw->rowCount() >= nRows) {
            tick(holdLoad, 25.0f);
        } else {
            size_t i = sw->rowCount();
            float l = loads[i];
            if (noisy && noisy[i]) l += (par++ % 2) ? 2.0f : 0.0f;
            tick(l, 25.0f);
        }
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
}

static void runConstantSweep(float holdLoad, float amb = 25.0f) {
    startAt(0.0f);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) tick(holdLoad, amb);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
}

void test_AC5_table_printed_after_done_in_order_with_header_and_columns() {
    startAt(0.0f);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) tick(25.75f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    size_t done = findPrefix("SWEEP DONE");
    TEST_ASSERT_TRUE(done != kNpos);
    size_t head = findPrefix("TABLE", done);
    TEST_ASSERT_EQUAL_UINT32(done + 1, head);
    TEST_ASSERT_TRUE(sink.lines[head].find("offset=0.75") != std::string::npos);
    size_t next = 0;
    expectTableAt(head, &next);
    TEST_ASSERT_EQUAL_UINT32(11, sw->rowCount());
    TEST_ASSERT_TRUE(next < sink.lines.size());
    TEST_ASSERT_TRUE(sink.lines[next].compare(0, 6, "TARGET") == 0);
    // the 25 % step shows 24.98 and its 1023 counts; every row shows corrected load 25.0
    TEST_ASSERT_TRUE(sink.lines[head + 2 + 10].compare(0, 10, "24.98 1023") == 0);
    TEST_ASSERT_EQUAL_STRING("0.00 0 25.0 25.0 941.2 923.0 yes", sink.lines[head + 2].c_str());
    TEST_ASSERT_TRUE(sink.lines[head + 2 + 1].compare(0, 7, "2.49 10") == 0);
}

void test_AC5_table_marks_unsettled_rows_no() {
    float loads[2] = {40.0f, 50.0f};
    bool noisy[2] = {true, false};
    runRows(22.5f, loads, noisy, 2);
    TEST_ASSERT_EQUAL_UINT32(2, sw->rowCount());
    TEST_ASSERT_FALSE(sw->row(0).settled);
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    size_t len = sink.lines[head + 2].size();
    TEST_ASSERT_TRUE(sink.lines[head + 2].compare(len - 3, 3, " no") == 0);
    len = sink.lines[head + 3].size();
    TEST_ASSERT_TRUE(sink.lines[head + 3].compare(len - 4, 4, " yes") == 0);
}

void test_AC5_table_for_start_n_has_only_recorded_rows() {
    float loads[7] = {30, 31, 32, 33, 34, 35, 36};
    runRows(10.0f, loads, nullptr, 7);
    TEST_ASSERT_EQUAL_UINT32(7, sw->rowCount());
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    TEST_ASSERT_TRUE(sink.lines[head + 2].compare(0, 5, "10.00") == 0 || sink.lines[head + 2].compare(0, 5, "9.99 ") == 0 ||
                     sink.lines[head + 2].compare(0, 5, "10.01") == 0);
}

void test_AC6_AC7_linear_process_interpolates_55_60_65_within_0_1_percent() {
    // T = 25 + 200 * counts / 4095 (+ 0.4 sensor bias removed by the offset): 55,60,65 C at 15, 17.5, 20 %.
    FirstOrderProcess proc(25.0f, 200.0f, 10.0f);
    const float bias = 0.4f;
    sw->onPeriod(1000, pr(proc.tempC + bias), pr(25.0f));
    sw->onCommand(startCmd(0.0f), 1000);
    g_now = 1000;
    sink.lines.clear();
    int ticks = 0;
    while (sw->state() != SweepState::Done && sw->state() != SweepState::Cutoff && ticks < 20000) {
        proc.step(sw->dutyCounts());
        tick(proc.tempC + bias, 25.0f);
        ticks++;
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    const float want[3] = {15.0f, 17.5f, 20.0f};
    for (int k = 0; k < 3; k++) {
        float d = 0.0f;
        unsigned c = 0;
        TEST_ASSERT_TRUE(parseTarget(next + k, kTargets[k], &d, &c));
        TEST_ASSERT_FLOAT_WITHIN(0.1f, want[k], d);
        TEST_ASSERT_TRUE((int)c >= (int)percentToCounts(want[k]) - 5);
        TEST_ASSERT_TRUE((int)c <= (int)percentToCounts(want[k]) + 5);
    }
}

void test_AC6_target_outside_table_is_not_reachable() {
    runConstantSweep(25.0f);  // every row 25 C: 55, 60, 65 are above the table
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    TEST_ASSERT_EQUAL_STRING("TARGET 55.0 C: not reachable within the table", sink.lines[next].c_str());
    TEST_ASSERT_EQUAL_STRING("TARGET 60.0 C: not reachable within the table", sink.lines[next + 1].c_str());
    TEST_ASSERT_EQUAL_STRING("TARGET 65.0 C: not reachable within the table", sink.lines[next + 2].c_str());
}

void test_AC6_partly_reachable_only_65_beyond_table() {
    // loads 40..62 monotonic: 55 and 60 reachable, 65 not
    float loads[7] = {40, 45, 50, 54, 58, 60, 62};
    runRows(10.0f, loads, nullptr, 7);
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    expectTargetsMatchReference(next);
    float d;
    unsigned c;
    TEST_ASSERT_TRUE(parseTarget(next, 55.0f, &d, &c));
    TEST_ASSERT_TRUE(parseTarget(next + 1, 60.0f, &d, &c));
    TEST_ASSERT_FALSE(parseTarget(next + 2, 65.0f, &d, &c));
}

void test_E5_fewer_than_two_settled_rows_is_not_reachable_single_row() {
    float loads[1] = {60.0f};
    runRows(25.0f, loads, nullptr, 1);
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    for (int k = 0; k < 3; k++) {
        float d;
        unsigned c;
        TEST_ASSERT_FALSE(parseTarget(next + k, kTargets[k], &d, &c));
    }
}

void test_E5_only_one_settled_row_of_two_is_not_reachable() {
    float loads[2] = {54.0f, 70.0f};
    bool noisy[2] = {true, false};  // first row never settles
    runRows(22.5f, loads, noisy, 2);
    TEST_ASSERT_EQUAL_UINT32(2, sw->rowCount());
    TEST_ASSERT_FALSE(sw->row(0).settled);
    TEST_ASSERT_TRUE(sw->row(1).settled);
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    for (int k = 0; k < 3; k++) {
        float d;
        unsigned c;
        TEST_ASSERT_FALSE(parseTarget(next + k, kTargets[k], &d, &c));
    }
}

void test_E5_unsettled_rows_are_skipped_and_neighbours_paired() {
    float loads[3] = {50.0f, 41.0f, 70.0f};
    bool noisy[3] = {false, true, false};
    runRows(20.0f, loads, noisy, 3);
    TEST_ASSERT_EQUAL_UINT32(3, sw->rowCount());
    TEST_ASSERT_FALSE(sw->row(1).settled);
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    expectTargetsMatchReference(next);
    // pair (50 C @ 20 %, 70 C @ 25 %): 55 C -> 21.25 %, 60 C -> 22.5 %, 65 C -> 23.75 % (within 0.1)
    const float want[3] = {21.25f, 22.5f, 23.75f};
    for (int k = 0; k < 3; k++) {
        float d = 0.0f;
        unsigned c = 0;
        TEST_ASSERT_TRUE(parseTarget(next + k, kTargets[k], &d, &c));
        TEST_ASSERT_FLOAT_WITHIN(0.1f, want[k], d);
    }
}

void test_E5_non_monotonic_table_uses_first_bracketing_pair() {
    // 10, 12.5, 15, ... % with loads 30, 56, 52, 70, ...: 55 C is bracketed by (30,56) first, also by
    // (56,52) and (52,70) later. First pair: 10 + 25*2.5/26 = ~12.4 %; later pair would give ~13.1 %.
    float loads[7] = {30, 56, 52, 70, 72, 74, 76};
    runRows(10.0f, loads, nullptr, 7);
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    expectTargetsMatchReference(next);
    float d = 0.0f;
    unsigned c = 0;
    TEST_ASSERT_TRUE(parseTarget(next, 55.0f, &d, &c));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.4f, d);
    // 60 C: first bracketing pair is (52,70) at 15 %,17.5 % -> ~16.1 %
    TEST_ASSERT_TRUE(parseTarget(next + 1, 60.0f, &d, &c));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.1f, d);
}

void test_AC6_target_equal_to_row_load_gives_that_rows_duty() {
    float loads[3] = {50.0f, 60.0f, 70.0f};
    runRows(20.0f, loads, nullptr, 3);
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    float d = 0.0f;
    unsigned c = 0;
    TEST_ASSERT_TRUE(parseTarget(next + 1, 60.0f, &d, &c));
    TEST_ASSERT_FLOAT_WITHIN(0.06f, countsToPercent(sw->row(1).dutyCounts), d);
}

void test_AC7_results_when_never_run_says_no_results_yet() {
    sw->onCommand(cmd(CmdType::Results), 100);
    TEST_ASSERT_TRUE(sink.anyHas("no results yet"));
    TEST_ASSERT_EQUAL_UINT32(kNpos, findPrefix("TABLE"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC7_results_during_hold_says_no_results_yet() {
    startAt(0.0f);
    tickN(10, 25.0f, 25.0f);
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Results), g_now);
    TEST_ASSERT_TRUE(sink.anyHas("no results yet"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_AC7_results_after_done_reprints_table_and_targets() {
    float loads[7] = {40, 45, 50, 54, 58, 60, 62};
    runRows(10.0f, loads, nullptr, 7);
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Results), g_now);
    size_t head = findPrefix("TABLE");
    TEST_ASSERT_EQUAL_UINT32(0, head);
    size_t next = 0;
    expectTableAt(head, &next);
    TEST_ASSERT_TRUE(sink.lines[head].find("offset=0.00") != std::string::npos);
    expectTargetsMatchReference(next);
    TEST_ASSERT_EQUAL_UINT32(next + 3, sink.lines.size());
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    TEST_ASSERT_EQUAL_UINT32(7, sw->rowCount());
}

void test_AC7_results_mid_run_prints_table_without_targets_and_keeps_running() {
    startAt(0.0f);
    tickN(120 * 3, 25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(3, sw->rowCount());
    uint16_t duty = sw->dutyCounts();
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Results), g_now);
    size_t next = 0;
    expectTableAt(findPrefix("TABLE"), &next);
    TEST_ASSERT_EQUAL_UINT32(kNpos, findPrefix("TARGET"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_EQUAL_UINT16(duty, sw->dutyCounts());
}

void test_AC7_results_after_stop_prints_table_without_targets() {
    startAt(0.0f);
    tickN(120 * 3, 25.0f, 25.0f);
    sw->onCommand(cmd(CmdType::Stop), g_now);
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Results), g_now);
    size_t next = 0;
    expectTableAt(findPrefix("TABLE"), &next);
    TEST_ASSERT_EQUAL_UINT32(3, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT32(kNpos, findPrefix("TARGET"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_AC7_results_after_cutoff_prints_table_without_targets() {
    startAt(0.0f);
    tickN(120 * 2, 25.0f, 25.0f);
    tick(80.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Results), g_now);
    size_t next = 0;
    expectTableAt(findPrefix("TABLE"), &next);
    TEST_ASSERT_EQUAL_UINT32(2, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT32(kNpos, findPrefix("TARGET"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

// Forwards lines to a RecSink unless `drop` is set (a client that is not listening).
struct DropSink : LineSink {
    RecSink* inner = nullptr;
    bool drop = false;
    void line(const char* text) override {
        if (!drop) inner->line(text);
    }
};
static DropSink dropSink;

void test_E10_results_prints_full_table_after_output_was_discarded() {
    delete sw;
    dropSink.inner = &sink;
    dropSink.drop = false;
    sw = new Sweep(dropSink);
    sw->begin(0);
    float loads[7] = {40, 45, 50, 54, 58, 60, 62};
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sw->onCommand(startCmd(10.0f), 1000);
    g_now = 1000;
    dropSink.drop = true;
    sink.lines.clear();
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) {
        float l = (sw->state() == SweepState::HoldZero) ? 25.0f : loads[sw->rowCount()];
        tick(l, 25.0f);
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());  // nothing reached the client
    dropSink.drop = false;
    sw->onCommand(cmd(CmdType::Results), g_now);
    size_t next = 0;
    expectTableAt(findPrefix("TABLE"), &next);
    expectTargetsMatchReference(next);
    TEST_ASSERT_EQUAL_UINT32(7, sw->rowCount());
    TEST_ASSERT_FALSE(sink.anyHas("no results yet"));
}

void test_E14_new_start_after_done_discards_old_table() {
    runConstantSweep(25.0f);
    TEST_ASSERT_EQUAL_UINT32(11, sw->rowCount());
    sink.lines.clear();
    sw->onCommand(startCmd(0.0f), g_now);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_FALSE(sw->offsetKnown());
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Results), g_now);
    TEST_ASSERT_TRUE(sink.anyHas("no results yet"));
    TEST_ASSERT_EQUAL_UINT32(kNpos, findPrefix("TABLE"));
    // new sweep records its first row, then results lists exactly that one row
    tickN(120, 25.5f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Results), g_now);
    size_t head = findPrefix("TABLE");
    TEST_ASSERT_TRUE(head != kNpos);
    TEST_ASSERT_TRUE(sink.lines[head].find("offset=0.50") != std::string::npos);
    size_t next = 0;
    expectTableAt(head, &next);
    TEST_ASSERT_EQUAL_UINT32(head + 3, next);
    TEST_ASSERT_EQUAL_UINT32(kNpos, findPrefix("TARGET"));
}

// ---------------------------------------------------------------------------
// T8: periodic LOG line (AC11), duty cap over a whole sweep (AC10), G1 end to end
// ---------------------------------------------------------------------------

struct LogRec {
    unsigned t;
    float duty;
    unsigned counts;
    float load, ambient, loadAdc, ambientAdc;
};

static bool parseLog(const std::string& l, LogRec* r) {
    if (l.compare(0, 4, "LOG ") != 0) return false;
    int n = sscanf(l.c_str(), "LOG t=%u duty=%f%% (%u) load=%f C ambient=%f C load_adc=%f ambient_adc=%f", &r->t,
                   &r->duty, &r->counts, &r->load, &r->ambient, &r->loadAdc, &r->ambientAdc);
    return n == 7;
}

static size_t countLogs() {
    size_t n = 0;
    for (const auto& l : sink.lines)
        if (l.compare(0, 3, "LOG") == 0) n++;
    return n;
}

// Returns the index-th LOG line (asserting it parses).
static LogRec nthLog(size_t index) {
    size_t seen = 0;
    for (const auto& l : sink.lines) {
        if (l.compare(0, 3, "LOG") != 0) continue;
        if (seen++ == index) {
            LogRec r{};
            TEST_ASSERT_TRUE_MESSAGE(parseLog(l, &r), l.c_str());
            return r;
        }
    }
    TEST_FAIL_MESSAGE("LOG line index not present");
    return LogRec{};
}

void test_AC11_no_log_before_5_seconds_and_first_log_at_t5() {
    startAt(0.0f);
    for (int i = 1; i <= 4; i++) {
        tick(25.0f, 25.0f);
        TEST_ASSERT_EQUAL_UINT32(0, countLogs());
    }
    tick(25.0f, 25.0f);  // t = 5
    TEST_ASSERT_EQUAL_UINT32(1, countLogs());
    TEST_ASSERT_EQUAL_UINT32(5, nthLog(0).t);
}

void test_AC11_log_exactly_every_5_seconds_with_one_second_periods() {
    startAt(0.0f);
    for (int i = 1; i <= 14; i++) tick(25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(2, countLogs());
    TEST_ASSERT_EQUAL_UINT32(5, nthLog(0).t);
    TEST_ASSERT_EQUAL_UINT32(10, nthLog(1).t);
    tick(25.0f, 25.0f);  // t = 15
    TEST_ASSERT_EQUAL_UINT32(3, countLogs());
    TEST_ASSERT_EQUAL_UINT32(15, nthLog(2).t);
}

void test_AC11_log_line_format_in_hold_uses_uncorrected_load() {
    startAt(0.0f);
    for (int i = 1; i <= 5; i++) tick(25.3f, 25.0f, 941.2f, 923.0f);
    TEST_ASSERT_EQUAL_UINT32(1, countLogs());
    size_t idx = findPrefix("LOG");
    TEST_ASSERT_TRUE(idx != kNpos);
    TEST_ASSERT_EQUAL_STRING("LOG t=5 duty=0.00% (0) load=25.3 C ambient=25.0 C load_adc=941.2 ambient_adc=923.0",
                             sink.lines[idx].c_str());
}

void test_AC11_log_reports_raw_counts_and_both_temperatures() {
    startAt(0.0f);
    for (int i = 1; i <= 5; i++) tick(26.4f, 24.8f, 1000.5f, 870.3f);
    size_t idx = findPrefix("LOG");
    TEST_ASSERT_TRUE(idx != kNpos);
    TEST_ASSERT_EQUAL_STRING("LOG t=5 duty=0.00% (0) load=26.4 C ambient=24.8 C load_adc=1000.5 ambient_adc=870.3",
                             sink.lines[idx].c_str());
}

void test_AC11_log_t_is_whole_seconds_since_start_with_irregular_periods() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sw->onCommand(startCmd(0.0f), 1000);
    sink.lines.clear();
    // 1.2 s periods: 2.2 3.4 4.6 5.8 -> first multiple of 5 reached at +4.8? check exact offsets below
    const uint32_t offs[] = {1200, 2400, 3600, 4800, 4999, 5000, 6200, 9600, 9999, 10000, 10800, 14999, 15000};
    const unsigned expectLogT[] = {5, 10, 15};
    const uint32_t logAt[] = {5000, 10000, 15000};
    size_t logsSeen = 0;
    for (size_t i = 0; i < sizeof offs / sizeof offs[0]; i++) {
        sw->onPeriod(1000 + offs[i], pr(25.0f), pr(25.0f));
        bool shouldLog = false;
        for (size_t k = 0; k < 3; k++)
            if (offs[i] == logAt[k]) shouldLog = true;
        if (shouldLog) {
            logsSeen++;
            TEST_ASSERT_EQUAL_UINT32(logsSeen, countLogs());
            TEST_ASSERT_EQUAL_UINT32(expectLogT[logsSeen - 1], nthLog(logsSeen - 1).t);
        } else {
            TEST_ASSERT_EQUAL_UINT32(logsSeen, countLogs());
        }
    }
}

void test_AC11_log_fires_in_period_that_first_reaches_the_multiple_not_later() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sw->onCommand(startCmd(0.0f), 1000);
    sink.lines.clear();
    // 1.2 s periods: whole seconds 1,2,3,4,6(t=6 first >= 5),7,8,9,10(at 10.8),12,13,14,15(15.6)
    uint32_t now = 1000;
    unsigned lastT = 0;
    size_t logs = 0;
    for (int i = 1; i <= 13; i++) {
        now += 1200;
        sw->onPeriod(now, pr(25.0f), pr(25.0f));
        unsigned whole = (unsigned)((now - 1000) / 1000);
        size_t want = whole / 5;  // multiples of 5 reached so far
        if (want > logs) logs = 1 + logs;  // at most one line per period
        TEST_ASSERT_EQUAL_UINT32(logs, countLogs());
        if (countLogs() > 0) lastT = nthLog(countLogs() - 1).t;
    }
    TEST_ASSERT_EQUAL_UINT32(3, countLogs());
    TEST_ASSERT_EQUAL_UINT32(15, lastT);
}

void test_AC11_log_duty_is_the_one_applied_after_a_step_change_in_the_same_call() {
    startAt(0.0f);
    tickN(115, 25.0f, 25.0f);  // t = 115
    TEST_ASSERT_EQUAL_UINT32(23, countLogs());
    LogRec before = nthLog(22);
    TEST_ASSERT_EQUAL_UINT32(115, before.t);
    TEST_ASSERT_EQUAL_UINT32(0, before.counts);
    tickN(5, 25.0f, 25.0f);  // t = 120: hold settles in this call, step to 2.5 % applies
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_EQUAL_UINT16(102, sw->dutyCounts());
    LogRec at120 = nthLog(23);
    TEST_ASSERT_EQUAL_UINT32(120, at120.t);
    TEST_ASSERT_EQUAL_UINT32(102, at120.counts);
    size_t idx = 0;
    for (size_t i = 0; i < sink.lines.size(); i++)
        if (sink.lines[i].compare(0, 10, "LOG t=120 ") == 0) idx = i + 1;
    TEST_ASSERT_TRUE(idx != 0);
    TEST_ASSERT_TRUE(sink.lines[idx - 1].find("duty=2.49% (102)") != std::string::npos);
}

void test_AC11_log_load_is_offset_corrected_once_offset_is_known() {
    startAt(0.0f);
    tickN(115, 25.5f, 25.0f);
    TEST_ASSERT_FALSE(sw->offsetKnown());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.5f, nthLog(22).load);  // t = 115, uncorrected
    tickN(5, 25.5f, 25.0f);                                    // t = 120, offset 0.5 known
    TEST_ASSERT_TRUE(sw->offsetKnown());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, nthLog(23).load);  // t = 120, corrected
    tickN(5, 25.5f, 25.0f);                                    // t = 125
    LogRec r = nthLog(24);
    TEST_ASSERT_EQUAL_UINT32(125, r.t);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.load);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.ambient);
}

void test_AC11_no_log_in_idle_even_when_periods_continue() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sink.lines.clear();
    g_now = 1000;
    tickN(30, 25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(0, countLogs());
}

void test_AC11_no_log_in_the_call_that_ends_the_sweep_nor_afterwards() {
    startAt(0.0f);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 5000) tick(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    unsigned tFinal = (unsigned)((g_now - 1000) / 1000);
    // one line per multiple of 5 strictly before the ending call
    TEST_ASSERT_EQUAL_UINT32((tFinal - 1) / 5, countLogs());
    for (size_t i = 0; i < countLogs(); i++) {
        LogRec r = nthLog(i);
        TEST_ASSERT_EQUAL_UINT32((i + 1) * 5, r.t);
        TEST_ASSERT_TRUE(r.t < tFinal);
    }
    size_t done = findPrefix("SWEEP DONE");
    TEST_ASSERT_TRUE(done != kNpos);
    for (size_t i = done; i < sink.lines.size(); i++) TEST_ASSERT_TRUE(sink.lines[i].compare(0, 3, "LOG") != 0);
    size_t before = sink.lines.size();
    tickN(20, 25.0f, 25.0f);
    for (size_t i = before; i < sink.lines.size(); i++) TEST_ASSERT_TRUE(sink.lines[i].compare(0, 3, "LOG") != 0);
}

void test_AC11_no_log_in_cutoff_call_nor_afterwards() {
    startAt(0.0f);
    tickN(4, 25.0f, 25.0f);
    tick(80.0f, 25.0f, 1800.0f, 923.0f);  // t = 5: cutoff instead of a log
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT32(0, countLogs());
    tickN(20, 25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(0, countLogs());
}

void test_AC11_stop_ends_logging_and_new_start_restarts_the_5_second_count() {
    startAt(0.0f);
    tickN(7, 25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(1, countLogs());
    sw->onCommand(cmd(CmdType::Stop), g_now);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    sink.lines.clear();
    tickN(12, 25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(0, countLogs());
    // restart at the current time: count begins from this start
    sw->onCommand(startCmd(0.0f), g_now);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    sink.lines.clear();
    tickN(4, 25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(0, countLogs());
    tick(25.0f, 25.0f);
    TEST_ASSERT_EQUAL_UINT32(1, countLogs());
    TEST_ASSERT_EQUAL_UINT32(5, nthLog(0).t);
}

void test_AC11_log_percent_text_matches_counts() {
    startAt(0.0f);
    tickN(300, 25.0f, 25.0f);  // crosses several steps
    TEST_ASSERT_EQUAL_UINT32(60, countLogs());
    for (size_t i = 0; i < sink.lines.size(); i++) {
        LogRec r{};
        if (!parseLog(sink.lines[i], &r)) continue;
        char buf[24];
        snprintf(buf, sizeof buf, "duty=%.2f%% (%u)", (double)countsToPercent((uint16_t)r.counts), r.counts);
        TEST_ASSERT_TRUE_MESSAGE(sink.lines[i].find(buf) != std::string::npos, sink.lines[i].c_str());
    }
}

// Runs a complete simulated sweep checking the cap after every onPeriod and onCommand.
static void runCappedSweep(float startPct) {
    FirstOrderProcess proc(25.0f, 200.0f, 10.0f);
    sw->onPeriod(1000, pr(proc.tempC), pr(25.0f));
    TEST_ASSERT_TRUE(sw->dutyCounts() <= kCapCounts);
    sw->onCommand(startCmd(startPct), 1000);
    TEST_ASSERT_TRUE(sw->dutyCounts() <= kCapCounts);
    g_now = 1000;
    sink.lines.clear();
    int ticks = 0;
    while (sw->state() != SweepState::Done && sw->state() != SweepState::Cutoff && ticks < 40000) {
        proc.step(sw->dutyCounts());
        tick(proc.tempC, 25.0f);
        TEST_ASSERT_TRUE(sw->dutyCounts() <= 1023);
        if (ticks % 97 == 0) {
            sw->onCommand(cmd(CmdType::Results), g_now);
            TEST_ASSERT_TRUE(sw->dutyCounts() <= 1023);
        }
        ticks++;
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    sw->onCommand(cmd(CmdType::Results), g_now);
    TEST_ASSERT_TRUE(sw->dutyCounts() <= 1023);
    TEST_ASSERT_TRUE(countLogs() > 0);
    unsigned maxSeen = 0;
    for (const auto& l : sink.lines) {
        LogRec r{};
        if (parseLog(l, &r)) {
            TEST_ASSERT_TRUE(r.counts <= 1023);
            if (r.counts > maxSeen) maxSeen = r.counts;
        }
    }
    for (size_t i = 0; i < sw->rowCount(); i++) TEST_ASSERT_TRUE(sw->row(i).dutyCounts <= 1023);
    TEST_ASSERT_EQUAL_UINT32(1023, maxSeen);  // the last step is applied as exactly the cap
}

void test_AC10_AC11_start_never_exceeds_1023_counts_in_period_log_or_table() { runCappedSweep(0.0f); }
void test_AC10_AC11_start_10_never_exceeds_1023_counts_in_period_log_or_table() { runCappedSweep(10.0f); }
void test_AC10_AC11_start_25_never_exceeds_1023_counts_in_period_log_or_table() { runCappedSweep(25.0f); }

void test_G1_end_to_end_simulated_sweep_reports_three_duties_within_half_percent() {
    // T = 22 + 190 * d: 55, 60, 65 C at 17.37, 20.00, 22.63 %; maximum 22 + 47.5 = 69.5 C.
    FirstOrderProcess proc(22.0f, 190.0f, 20.0f);
    const float bias = 0.4f;
    sw->onPeriod(1000, pr(proc.tempC + bias), pr(22.0f));
    sw->onCommand(startCmd(0.0f), 1000);
    g_now = 1000;
    sink.lines.clear();
    int ticks = 0;
    while (sw->state() != SweepState::Done && sw->state() != SweepState::Cutoff && ticks < 40000) {
        proc.step(sw->dutyCounts());
        tick(proc.tempC + bias, 22.0f);
        TEST_ASSERT_TRUE(sw->dutyCounts() <= 1023);
        ticks++;
    }
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT32((unsigned)(ticks - 1) / 5, countLogs());
    size_t head = findPrefix("TABLE", findPrefix("SWEEP DONE"));
    size_t next = 0;
    expectTableAt(head, &next);
    for (int k = 0; k < 3; k++) {
        float d = 0.0f;
        unsigned c = 0;
        TEST_ASSERT_TRUE(parseTarget(next + k, kTargets[k], &d, &c));
        float want = (kTargets[k] - 22.0f) / 190.0f * 100.0f;
        TEST_ASSERT_FLOAT_WITHIN(0.5f, want, d);
    }
}


// ---- single place for the maximum duty: everything derives from sweepcfg::kCapPercent ----

static std::string capText() {
    char buf[32];
    snprintf(buf, sizeof buf, "%g", (double)sweepcfg::kCapPercent);
    return buf;
}

void test_AC1_cfg_ready_message_shows_configured_cap() {
    std::string want = "0.." + capText();
    bool found = false;
    for (const auto& l : sink.lines) {
        size_t p = l.find("0..");
        if (p == std::string::npos) continue;
        TEST_ASSERT_TRUE_MESSAGE(l.compare(p, want.size(), want) == 0, l.c_str());
        found = true;
    }
    TEST_ASSERT_TRUE(found);
}

void test_E13_cfg_bad_start_usage_shows_configured_cap() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::BadStart), 1100);
    std::string want = "N from 0 to " + capText();
    TEST_ASSERT_TRUE(sink.anyHas(want.c_str()));
}

void test_AC2_AC10_cfg_full_sweep_last_step_is_cap_and_row_count_derived() {
    startAt(0.0f);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 20000) tick(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    size_t expectRows = 1 + (size_t)(sweepcfg::kCapPercent / sweepcfg::kStepPercent + 1e-4f);
    TEST_ASSERT_EQUAL_UINT32(expectRows, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, sw->row(sw->rowCount() - 1).dutyCounts);
}

void test_E13_cfg_start_at_cap_percent_runs_exactly_one_step_at_cap() {
    startAt(sweepcfg::kCapPercent);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    int guard = 0;
    while (sw->state() != SweepState::Done && guard++ < 20000) tick(25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Done);
    TEST_ASSERT_EQUAL_UINT32(1, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, sw->row(0).dutyCounts);
}

// ---- T8: Sweep::observe and the new ready / unknown-command words (plan D7, D8) ----

static bool hasExactLine(const char* want) {
    for (const auto& l : sink.lines)
        if (l == want) return true;
    return false;
}

static std::string tempReply() {
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Temp), 5000);
    TEST_ASSERT_EQUAL_UINT32(1, sink.lines.size());
    return sink.lines[0];
}

void test_T8_observe_makes_temp_print_latest_values() {
    sw->observe(pr(25.3f, 941.2f, 398), pr(24.8f, 923.0f, 397));
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Temp), 1100);
    TEST_ASSERT_TRUE(sink.anyHas("TEMP"));
    TEST_ASSERT_FALSE(sink.anyHas("no measurement"));
    TEST_ASSERT_TRUE(sink.anyHas("25.3"));
    TEST_ASSERT_TRUE(sink.anyHas("24.8"));
    TEST_ASSERT_TRUE(sink.anyHas("941.2"));
    TEST_ASSERT_TRUE(sink.anyHas("923.0"));
    TEST_ASSERT_TRUE(sink.anyHas("398"));
    TEST_ASSERT_TRUE(sink.anyHas("397"));
}

void test_T8_observe_temp_reply_equals_onPeriod_temp_reply() {
    sw->observe(pr(31.4f, 1000.0f, 120), pr(22.2f, 900.0f, 119));
    std::string viaObserve = tempReply();
    delete sw;
    sw = new Sweep(sink);
    sw->begin(0);
    sw->onPeriod(1000, pr(31.4f, 1000.0f, 120), pr(22.2f, 900.0f, 119));
    std::string viaPeriod = tempReply();
    TEST_ASSERT_EQUAL_STRING(viaPeriod.c_str(), viaObserve.c_str());
}

void test_T8_observe_prints_nothing() {
    sink.lines.clear();
    sw->observe(pr(25.0f), pr(25.0f));
    sw->observe(pr(80.0f, 1800.0f), pr(25.0f, 4095.0f));
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
}

void test_T8_observe_keeps_latest_values_only() {
    sw->observe(pr(25.3f, 941.2f, 398), pr(24.8f, 923.0f, 397));
    sw->observe(pr(26.7f, 950.0f, 200), pr(23.1f, 910.0f, 201));
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Temp), 1100);
    TEST_ASSERT_TRUE(sink.anyHas("26.7"));
    TEST_ASSERT_TRUE(sink.anyHas("23.1"));
    TEST_ASSERT_FALSE(sink.anyHas("25.3"));
    TEST_ASSERT_FALSE(sink.anyHas("24.8"));
}

void test_T8_observe_sets_measurement_seen_so_start_is_not_refused_for_missing_measurement() {
    sw->observe(pr(25.0f), pr(25.0f));
    sink.lines.clear();
    sw->onCommand(startCmd(), 1000);
    TEST_ASSERT_FALSE(sink.anyHas("no measurement"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_T8_observe_at_80_C_does_not_latch_cutoff() {
    sw->observe(pr(80.0f, 1800.0f), pr(25.0f));
    TEST_ASSERT_FALSE(sink.anyHas("CUTOFF"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    sw->observe(pr(95.0f, 1950.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_TRUE(tempReply().find("80.0") == std::string::npos);
}

void test_T8_observe_rail_counts_do_not_latch_cutoff() {
    sw->observe(pr(25.0f, 0.0f), pr(25.0f));
    sw->observe(pr(25.0f, 4095.0f), pr(25.0f));
    sw->observe(pr(25.0f), pr(25.0f, 0.0f));
    sw->observe(pr(25.0f), pr(25.0f, 4095.0f));
    TEST_ASSERT_FALSE(sink.anyHas("CUTOFF"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Idle);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    // not latched: a later start with a sane measurement is accepted
    sw->observe(pr(25.0f), pr(25.0f));
    sw->onCommand(startCmd(), 1000);
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
}

void test_T8_observe_while_running_leaves_state_and_duty_unchanged() {
    startAt(0.0f);
    tickN(120, 25.0f, 25.0f);
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    uint16_t duty = sw->dutyCounts();
    TEST_ASSERT_TRUE(duty > 0);
    size_t rows = sw->rowCount();
    sink.lines.clear();
    sw->observe(pr(85.0f, 1900.0f), pr(25.0f, 4095.0f));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Running);
    TEST_ASSERT_EQUAL_UINT16(duty, sw->dutyCounts());
    TEST_ASSERT_EQUAL_UINT32(rows, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
}

void test_T8_observe_does_not_advance_the_settle_window() {
    startAt(0.0f);
    for (int i = 0; i < 300; i++) sw->observe(pr(25.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sw->state() == SweepState::HoldZero);
    TEST_ASSERT_FALSE(sw->offsetKnown());
    TEST_ASSERT_EQUAL_UINT32(0, sw->rowCount());
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
}

void test_T8_onPeriod_still_latches_cutoff_after_observe_change() {
    sw->onPeriod(1000, pr(25.0f), pr(25.0f));
    sink.lines.clear();
    sw->onPeriod(2000, pr(80.0f, 1800.0f), pr(25.0f));
    TEST_ASSERT_TRUE(sink.oneLineHasAll("CUTOFF", "load"));
    TEST_ASSERT_TRUE(sw->state() == SweepState::Cutoff);
    TEST_ASSERT_EQUAL_UINT16(0, sw->dutyCounts());
    TEST_ASSERT_TRUE(tempReply().find("80.0") != std::string::npos);
}

void test_T8_ready_commands_line_lists_prbs_commands_exactly() {
    std::string want = "commands: start [N] (N = 0.." + capText() +
                       " %), stop, results, temp, prbs load N, prbs run";
    TEST_ASSERT_TRUE_MESSAGE(hasExactLine(want.c_str()), want.c_str());
}

void test_T8_unknown_command_reply_lists_prbs_commands_exactly() {
    sink.lines.clear();
    sw->onCommand(cmd(CmdType::Unknown), 100);
    const char* want =
        "UNKNOWN COMMAND: valid commands: start [N], stop, results, temp, prbs load N, prbs run";
    TEST_ASSERT_TRUE_MESSAGE(hasExactLine(want), want);
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
    RUN_TEST(test_AC3_hold_starts_at_zero_with_no_rows_or_offset);
    RUN_TEST(test_AC3_constant_signal_settles_exactly_at_120_periods);
    RUN_TEST(test_AC3_range_of_exactly_threshold_settles);
    RUN_TEST(test_AC3_range_above_threshold_does_not_settle);
    RUN_TEST(test_AC3_only_last_120_entries_count_for_range_and_offset);
    RUN_TEST(test_AC15_offset_is_mean_of_load_minus_ambient_and_printed_once);
    RUN_TEST(test_AC15_offset_uses_mean_not_last_value);
    RUN_TEST(test_AC15_start_zero_records_hold_as_row_zero_with_corrected_load);
    RUN_TEST(test_AC15_start_n_does_not_record_hold_as_row);
    RUN_TEST(test_AC15_offset_not_applied_to_cutoff_check);
    RUN_TEST(test_AC2_steps_run_ascending_from_zero_with_correct_counts);
    RUN_TEST(test_AC2_E8_full_constant_sweep_has_eleven_rows_ending_at_1023);
    RUN_TEST(test_E8_last_step_is_applied_as_1023_counts_while_running);
    RUN_TEST(test_AC2_start_n_runs_n_plus_k_times_2_5_up_to_25);
    RUN_TEST(test_AC2_start_n_not_multiple_does_not_force_last_step_to_25);
    RUN_TEST(test_AC4_step_not_settled_at_exactly_45_minutes);
    RUN_TEST(test_AC4_every_step_times_out_after_its_own_45_minutes);
    RUN_TEST(test_AC4_settling_is_checked_before_timeout);
    RUN_TEST(test_E7_cutoff_in_middle_of_window_records_nothing);
    RUN_TEST(test_E7_cutoff_on_the_period_that_would_settle_wins);
    RUN_TEST(test_E7_cutoff_in_later_step_keeps_earlier_rows_only);
    RUN_TEST(test_E6_each_row_keeps_its_own_ambient_and_offset_is_not_recomputed);
    RUN_TEST(test_AC2_AC3_AC15_simulated_first_order_process_full_sweep);
    RUN_TEST(test_AC2_stop_mid_sweep_then_new_start_discards_old_rows_and_offset);
    RUN_TEST(test_D4_AC3_zero_sample_periods_do_not_count_toward_settle_window);
    RUN_TEST(test_D4_AC3_zero_sample_ambient_periods_do_not_count_toward_settle_window);
    RUN_TEST(test_D4_AC15_stale_values_do_not_enter_offset_or_range);
    RUN_TEST(test_D4_AC8_zero_sample_periods_with_hot_held_value_do_not_cut_off);
    RUN_TEST(test_D4_AC9_zero_sample_periods_with_rail_held_counts_do_not_cut_off);
    RUN_TEST(test_D4_AC4_only_zero_sample_periods_still_time_out_at_2700_s);
    RUN_TEST(test_AC5_table_printed_after_done_in_order_with_header_and_columns);
    RUN_TEST(test_AC5_table_marks_unsettled_rows_no);
    RUN_TEST(test_AC5_table_for_start_n_has_only_recorded_rows);
    RUN_TEST(test_AC6_AC7_linear_process_interpolates_55_60_65_within_0_1_percent);
    RUN_TEST(test_AC6_target_outside_table_is_not_reachable);
    RUN_TEST(test_AC6_partly_reachable_only_65_beyond_table);
    RUN_TEST(test_E5_fewer_than_two_settled_rows_is_not_reachable_single_row);
    RUN_TEST(test_E5_only_one_settled_row_of_two_is_not_reachable);
    RUN_TEST(test_E5_unsettled_rows_are_skipped_and_neighbours_paired);
    RUN_TEST(test_E5_non_monotonic_table_uses_first_bracketing_pair);
    RUN_TEST(test_AC6_target_equal_to_row_load_gives_that_rows_duty);
    RUN_TEST(test_AC7_results_when_never_run_says_no_results_yet);
    RUN_TEST(test_AC7_results_during_hold_says_no_results_yet);
    RUN_TEST(test_AC7_results_after_done_reprints_table_and_targets);
    RUN_TEST(test_AC7_results_mid_run_prints_table_without_targets_and_keeps_running);
    RUN_TEST(test_AC7_results_after_stop_prints_table_without_targets);
    RUN_TEST(test_AC7_results_after_cutoff_prints_table_without_targets);
    RUN_TEST(test_E10_results_prints_full_table_after_output_was_discarded);
    RUN_TEST(test_E14_new_start_after_done_discards_old_table);
    RUN_TEST(test_AC11_no_log_before_5_seconds_and_first_log_at_t5);
    RUN_TEST(test_AC11_log_exactly_every_5_seconds_with_one_second_periods);
    RUN_TEST(test_AC11_log_line_format_in_hold_uses_uncorrected_load);
    RUN_TEST(test_AC11_log_reports_raw_counts_and_both_temperatures);
    RUN_TEST(test_AC11_log_t_is_whole_seconds_since_start_with_irregular_periods);
    RUN_TEST(test_AC11_log_fires_in_period_that_first_reaches_the_multiple_not_later);
    RUN_TEST(test_AC11_log_duty_is_the_one_applied_after_a_step_change_in_the_same_call);
    RUN_TEST(test_AC11_log_load_is_offset_corrected_once_offset_is_known);
    RUN_TEST(test_AC11_no_log_in_idle_even_when_periods_continue);
    RUN_TEST(test_AC11_no_log_in_the_call_that_ends_the_sweep_nor_afterwards);
    RUN_TEST(test_AC11_no_log_in_cutoff_call_nor_afterwards);
    RUN_TEST(test_AC11_stop_ends_logging_and_new_start_restarts_the_5_second_count);
    RUN_TEST(test_AC11_log_percent_text_matches_counts);
    RUN_TEST(test_AC10_AC11_start_never_exceeds_1023_counts_in_period_log_or_table);
    RUN_TEST(test_AC10_AC11_start_10_never_exceeds_1023_counts_in_period_log_or_table);
    RUN_TEST(test_AC10_AC11_start_25_never_exceeds_1023_counts_in_period_log_or_table);
    RUN_TEST(test_G1_end_to_end_simulated_sweep_reports_three_duties_within_half_percent);
    RUN_TEST(test_AC1_cfg_ready_message_shows_configured_cap);
    RUN_TEST(test_E13_cfg_bad_start_usage_shows_configured_cap);
    RUN_TEST(test_AC2_AC10_cfg_full_sweep_last_step_is_cap_and_row_count_derived);
    RUN_TEST(test_E13_cfg_start_at_cap_percent_runs_exactly_one_step_at_cap);
    RUN_TEST(test_T8_observe_makes_temp_print_latest_values);
    RUN_TEST(test_T8_observe_temp_reply_equals_onPeriod_temp_reply);
    RUN_TEST(test_T8_observe_prints_nothing);
    RUN_TEST(test_T8_observe_keeps_latest_values_only);
    RUN_TEST(test_T8_observe_sets_measurement_seen_so_start_is_not_refused_for_missing_measurement);
    RUN_TEST(test_T8_observe_at_80_C_does_not_latch_cutoff);
    RUN_TEST(test_T8_observe_rail_counts_do_not_latch_cutoff);
    RUN_TEST(test_T8_observe_while_running_leaves_state_and_duty_unchanged);
    RUN_TEST(test_T8_observe_does_not_advance_the_settle_window);
    RUN_TEST(test_T8_onPeriod_still_latches_cutoff_after_observe_change);
    RUN_TEST(test_T8_ready_commands_line_lists_prbs_commands_exactly);
    RUN_TEST(test_T8_unknown_command_reply_lists_prbs_commands_exactly);
    return UNITY_END();
}
