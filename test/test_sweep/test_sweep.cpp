#include <unity.h>
#include <string>
#include <vector>

#include "sweep.h"
#include "commands.h"
#include "measure.h"
#include "sim_process.h"

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
    return UNITY_END();
}
