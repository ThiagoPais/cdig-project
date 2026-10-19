#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "measure.h"
#include "sweep.h"
#include "duty.h"
#include "PrbsConfig.h"
#include "prbs.h"

namespace {

struct RecSink : LineSink {
    std::vector<std::string> lines;
    void line(const char* text) override { lines.push_back(text); }
};

RecSink sink;
Prbs* prbs = nullptr;

std::string fmt(const char* f, unsigned long a = 0, unsigned long b = 0, unsigned long c = 0) {
    char buf[160];
    snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

void feed(const std::string& s, uint32_t t) {
    for (char c : s) prbs->onLoadChar(c, t);
}

std::string val(unsigned v) { return std::to_string(v) + "\n"; }

void expectOneLine(const std::string& expected) {
    TEST_ASSERT_EQUAL_UINT32(1, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), sink.lines[0].c_str());
}

void expectRefusedClean(const std::string& expected) {
    expectOneLine(expected);
    TEST_ASSERT_FALSE(prbs->hasSequence());
    TEST_ASSERT_FALSE(prbs->loading());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
}

// Loads a full sequence with the terminator of the last value at time t, then
// waits the quiet window. Returns after tick(t + kLoadTailMs).
void loadFull(const std::vector<unsigned>& v, uint32_t t) {
    TEST_ASSERT_TRUE(prbs->beginLoad(v.size(), t));
    for (unsigned x : v) feed(val(x), t);
    prbs->tick(t + prbscfg::kLoadTailMs);
}

const unsigned LOW = kCapCounts / 3;
const unsigned HIGH = kCapCounts / 2;

// Starts a load of n and feeds the given good values; clears the sink.
void startLoad(uint32_t n, const std::vector<unsigned>& pre, uint32_t t = 1000) {
    TEST_ASSERT_TRUE(prbs->beginLoad(n, t));
    for (unsigned x : pre) feed(val(x), t);
    sink.lines.clear();
}

}  // namespace

void setUp() {
    sink.lines.clear();
    delete prbs;
    prbs = new Prbs(sink);
}
void tearDown() {}

void test_AC1_begin_load_prints_ready() {
    TEST_ASSERT_TRUE(prbs->beginLoad(6, 1000));
    expectOneLine("PRBS LOAD READY n=6");
    TEST_ASSERT_TRUE(prbs->loading());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Loading);
    TEST_ASSERT_FALSE(prbs->hasSequence());
}

void test_AC2_valid_sequence_levels_and_changes() {
    // L H H L L H -> 3 changes
    std::vector<unsigned> v = {LOW, HIGH, HIGH, LOW, LOW, HIGH};
    TEST_ASSERT_TRUE(prbs->beginLoad(6, 1000));
    for (unsigned x : v) feed(val(x), 2000);
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Confirming);
    TEST_ASSERT_TRUE(prbs->loading());
    sink.lines.clear();
    prbs->tick(2000 + prbscfg::kLoadTailMs - 1);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_FALSE(prbs->hasSequence());
    prbs->tick(2000 + prbscfg::kLoadTailMs);
    TEST_ASSERT_EQUAL_UINT32(2, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=%lu high=%lu changes=%lu", LOW, HIGH, 3).c_str(),
                             sink.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("PRBS LOADED n=6", sink.lines[1].c_str());
    TEST_ASSERT_TRUE(prbs->hasSequence());
    TEST_ASSERT_FALSE(prbs->loading());
}

void test_AC2_order_of_levels_does_not_matter() {
    loadFull({HIGH, LOW}, 1000);
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=%lu high=%lu changes=%lu", LOW, HIGH, 1).c_str(),
                             sink.lines[sink.lines.size() - 2].c_str());
}

void test_E5_all_equal_sequence() {
    loadFull({HIGH, HIGH, HIGH, HIGH}, 1000);
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=%lu high=%lu changes=%lu", HIGH, HIGH, 0).c_str(),
                             sink.lines[sink.lines.size() - 2].c_str());
    TEST_ASSERT_EQUAL_STRING("PRBS LOADED n=4", sink.lines.back().c_str());
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_E5_all_zero_sequence() {
    loadFull({0, 0, 0}, 1000);
    TEST_ASSERT_EQUAL_STRING("PRBS LEVELS low=0 high=0 changes=0", sink.lines[sink.lines.size() - 2].c_str());
    TEST_ASSERT_EQUAL_STRING("PRBS LOADED n=3", sink.lines.back().c_str());
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_E5_zero_as_one_of_two_levels() {
    loadFull({0, HIGH, 0}, 1000);
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=0 high=%lu changes=%lu", HIGH, 2).c_str(),
                             sink.lines[sink.lines.size() - 2].c_str());
}

void test_E5_single_value_sequence() {
    loadFull({LOW}, 1000);
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=%lu high=%lu changes=%lu", LOW, LOW, 0).c_str(),
                             sink.lines[sink.lines.size() - 2].c_str());
    TEST_ASSERT_EQUAL_STRING("PRBS LOADED n=1", sink.lines.back().c_str());
}

void test_AC2_cr_and_blanks_are_tolerated() {
    TEST_ASSERT_TRUE(prbs->beginLoad(2, 1000));
    feed("  " + std::to_string(LOW) + " \t\r\n", 1000);
    feed(std::to_string(HIGH) + "\r\n", 1000);
    sink.lines.clear();
    prbs->tick(1000 + prbscfg::kLoadTailMs);
    TEST_ASSERT_EQUAL_UINT32(2, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=%lu high=%lu changes=%lu", LOW, HIGH, 1).c_str(),
                             sink.lines[0].c_str());
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_value_equal_to_cap_is_accepted() {
    loadFull({kCapCounts, 0}, 1000);
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=0 high=%lu changes=%lu", (unsigned long)kCapCounts, 1).c_str(),
                             sink.lines[sink.lines.size() - 2].c_str());
}

void test_E12_bad_length_zero() {
    TEST_ASSERT_FALSE(prbs->beginLoad(0, 1000));
    expectRefusedClean("REFUSED: bad length 0 (must be 1..32000)");
}

void test_E12_bad_length_too_large() {
    TEST_ASSERT_FALSE(prbs->beginLoad(32001, 1000));
    expectRefusedClean("REFUSED: bad length 32001 (must be 1..32000)");
}

void test_E12_bad_length_clears_earlier_sequence() {
    loadFull({LOW, HIGH}, 1000);
    TEST_ASSERT_TRUE(prbs->hasSequence());
    sink.lines.clear();
    TEST_ASSERT_FALSE(prbs->beginLoad(32001, 5000));
    expectRefusedClean("REFUSED: bad length 32001 (must be 1..32000)");
}

void test_E12_bad_length_accepts_no_value() {
    prbs->beginLoad(0, 1000);
    sink.lines.clear();
    feed(val(LOW), 1000);
    prbs->tick(1000 + prbscfg::kLoadTailMs);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_FALSE(prbs->hasSequence());
}

void test_AC2_max_length_is_accepted() {
    TEST_ASSERT_TRUE(prbs->beginLoad(32000, 1000));
    expectOneLine("PRBS LOAD READY n=32000");
}

void test_E8_begin_load_discards_earlier_sequence() {
    loadFull({LOW, HIGH}, 1000);
    TEST_ASSERT_TRUE(prbs->hasSequence());
    TEST_ASSERT_TRUE(prbs->beginLoad(3, 5000));
    TEST_ASSERT_FALSE(prbs->hasSequence());
    TEST_ASSERT_TRUE(prbs->loading());
}

static void expectNotInteger(const char* text, unsigned idx, const std::vector<unsigned>& pre) {
    setUp();
    startLoad(10, pre);
    feed(std::string(text) + "\n", 1000);
    expectRefusedClean(fmt("REFUSED: value %lu is not a non-negative integer", idx));
}

void test_E11_text_line() { expectNotInteger("abc", 1, {}); }
void test_E11_decimal_point() { expectNotInteger("12.5", 2, {LOW}); }
void test_E11_trailing_decimal_point() { expectNotInteger("12.", 1, {}); }
void test_E11_exponent() { expectNotInteger("1e3", 1, {}); }
void test_E11_minus_sign() { expectNotInteger("-5", 3, {LOW, HIGH}); }
void test_E11_plus_sign() { expectNotInteger("+5", 1, {}); }
void test_E11_command_line() { expectNotInteger("stop", 2, {LOW}); }
void test_E11_blank_line() { expectNotInteger("", 2, {LOW}); }
void test_E11_blank_spaces_line() { expectNotInteger("   ", 1, {}); }
void test_E11_embedded_space() { expectNotInteger("1 2", 1, {}); }
void test_E11_line_longer_than_31_chars() {
    expectNotInteger(std::string(32, '1').c_str(), 1, {});
}

void test_E11_cr_only_blank_line() {
    startLoad(5, {LOW});
    feed("\r\n", 1000);
    expectRefusedClean("REFUSED: value 2 is not a non-negative integer");
}

void test_E11_refusal_clears_earlier_sequence() {
    loadFull({LOW, HIGH}, 1000);
    TEST_ASSERT_TRUE(prbs->hasSequence());
    startLoad(3, {}, 5000);
    TEST_ASSERT_FALSE(prbs->hasSequence());
    feed("x\n", 5000);
    expectRefusedClean("REFUSED: value 1 is not a non-negative integer");
}

void test_E11_characters_after_refusal_are_ignored() {
    startLoad(3, {});
    feed("x\n", 1000);
    sink.lines.clear();
    feed(val(LOW) + val(LOW) + val(LOW), 1000);
    prbs->tick(1000 + prbscfg::kLoadTailMs);
    prbs->tick(1000 + prbscfg::kLoadTimeoutMs + 10);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_FALSE(prbs->hasSequence());
    TEST_ASSERT_FALSE(prbs->loading());
}

void test_AC2_value_above_cap() {
    startLoad(4, {LOW});
    feed(val(kCapCounts + 1), 1000);
    expectRefusedClean(fmt("REFUSED: value 2 = %lu is above the cap %lu", (unsigned long)kCapCounts + 1,
                           (unsigned long)kCapCounts));
}

void test_AC2_value_above_cap_clears_earlier_sequence() {
    loadFull({LOW, HIGH}, 1000);
    startLoad(2, {}, 5000);
    feed(val(kCapCounts + 1), 5000);
    expectRefusedClean(fmt("REFUSED: value 1 = %lu is above the cap %lu", (unsigned long)kCapCounts + 1,
                           (unsigned long)kCapCounts));
}

void test_AC2_value_with_more_than_five_digits_is_above_cap() {
    startLoad(4, {});
    feed("100000\n", 1000);
    TEST_ASSERT_EQUAL_UINT32(1, sink.lines.size());
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines[0].find("REFUSED: value 1 = "));
    TEST_ASSERT_TRUE(sink.lines[0].find("is above the cap") != std::string::npos);
    TEST_ASSERT_FALSE(prbs->hasSequence());
    TEST_ASSERT_FALSE(prbs->loading());
}

void test_AC2_third_distinct_value() {
    startLoad(6, {LOW, HIGH, LOW});
    feed(val(LOW + 1), 1000);
    expectRefusedClean("REFUSED: more than two distinct values (value 4)");
}

void test_AC2_third_distinct_value_clears_earlier_sequence() {
    loadFull({LOW, HIGH}, 1000);
    startLoad(3, {0, 1}, 5000);
    feed(val(2), 5000);
    expectRefusedClean("REFUSED: more than two distinct values (value 3)");
}

void test_E7_timeout_before_any_value() {
    TEST_ASSERT_TRUE(prbs->beginLoad(3, 1000));
    sink.lines.clear();
    prbs->tick(1000 + prbscfg::kLoadTimeoutMs - 1);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_TRUE(prbs->loading());
    prbs->tick(1000 + prbscfg::kLoadTimeoutMs);
    expectRefusedClean("REFUSED: no data for 5 s (got 0 of 3 values)");
}

void test_E7_timeout_counts_accepted_values() {
    TEST_ASSERT_TRUE(prbs->beginLoad(5, 1000));
    feed(val(LOW), 1000);
    feed(val(HIGH), 1500);
    sink.lines.clear();
    prbs->tick(1500 + prbscfg::kLoadTimeoutMs - 1);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    prbs->tick(1500 + prbscfg::kLoadTimeoutMs);
    expectRefusedClean("REFUSED: no data for 5 s (got 2 of 5 values)");
}

void test_E7_partial_characters_do_not_reset_timer() {
    TEST_ASSERT_TRUE(prbs->beginLoad(5, 1000));
    feed(val(LOW), 1000);
    feed("12", 4000);  // partial line, no terminator
    sink.lines.clear();
    prbs->tick(1000 + prbscfg::kLoadTimeoutMs);
    expectRefusedClean("REFUSED: no data for 5 s (got 1 of 5 values)");
}

void test_E7_timeout_clears_earlier_sequence() {
    loadFull({LOW, HIGH}, 1000);
    startLoad(3, {}, 5000);
    TEST_ASSERT_FALSE(prbs->hasSequence());
    prbs->tick(5000 + prbscfg::kLoadTimeoutMs);
    expectRefusedClean("REFUSED: no data for 5 s (got 0 of 3 values)");
}

void test_E7_tick_when_idle_prints_nothing() {
    prbs->tick(100000);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
}

void test_AC2_extra_character_in_quiet_window() {
    TEST_ASSERT_TRUE(prbs->beginLoad(2, 1000));
    feed(val(LOW) + val(HIGH), 1000);
    sink.lines.clear();
    prbs->onLoadChar('7', 1000 + prbscfg::kLoadTailMs - 1);
    expectRefusedClean("REFUSED: more values than announced (2)");
}

void test_AC2_extra_newline_in_quiet_window() {
    TEST_ASSERT_TRUE(prbs->beginLoad(2, 1000));
    feed(val(LOW) + val(HIGH), 1000);
    sink.lines.clear();
    prbs->onLoadChar('\n', 1010);
    expectRefusedClean("REFUSED: more values than announced (2)");
}

void test_AC2_extra_character_clears_earlier_sequence_and_stores_nothing() {
    loadFull({LOW, HIGH}, 1000);
    TEST_ASSERT_TRUE(prbs->beginLoad(1, 5000));
    feed(val(LOW), 5000);
    sink.lines.clear();
    prbs->onLoadChar('1', 5010);
    expectRefusedClean("REFUSED: more values than announced (1)");
    sink.lines.clear();
    prbs->tick(5000 + prbscfg::kLoadTailMs + 10);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_FALSE(prbs->hasSequence());
}

void test_E8_second_load_replaces_first() {
    loadFull({LOW, HIGH, LOW}, 1000);
    TEST_ASSERT_TRUE(prbs->hasSequence());
    sink.lines.clear();
    loadFull({0, 0, HIGH, HIGH}, 9000);
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());  // READY + LEVELS + LOADED
    TEST_ASSERT_EQUAL_STRING("PRBS LOAD READY n=4", sink.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=0 high=%lu changes=%lu", HIGH, 1).c_str(), sink.lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("PRBS LOADED n=4", sink.lines[2].c_str());
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_D2_max_length_pseudo_random_sequence() {
    const uint32_t N = prbscfg::kMaxValues;
    std::vector<unsigned> v(N);
    uint32_t s = 0x1234ABCDu;
    unsigned changes = 0;
    for (uint32_t i = 0; i < N; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;  // xorshift32, fixed seed
        v[i] = (s & 0x100u) ? HIGH : LOW;
        if (i > 0 && v[i] != v[i - 1]) changes++;
    }
    TEST_ASSERT_TRUE(prbs->beginLoad(N, 0));
    for (uint32_t i = 0; i < N; i++) feed(val(v[i]), 100);
    sink.lines.clear();
    prbs->tick(100 + prbscfg::kLoadTailMs);
    TEST_ASSERT_EQUAL_UINT32(2, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(fmt("PRBS LEVELS low=%lu high=%lu changes=%lu", LOW, HIGH, changes).c_str(),
                             sink.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("PRBS LOADED n=32000", sink.lines[1].c_str());
    TEST_ASSERT_TRUE(prbs->hasSequence());
}


// ---------------------------------------------------------------- T10: run playback

namespace {

PeriodResult R(float t, uint16_t samples = 10) {
    PeriodResult r;
    r.tempC = t;
    r.meanCount = 700.0f;
    r.samples = samples;
    return r;
}

// "k,time_s,u,load,ambient" with one-decimal temperatures given as text.
std::string dataLine(unsigned k, unsigned t, unsigned u, const char* load, const char* amb) {
    return std::to_string(k) + "," + std::to_string(t) + "," + std::to_string(u) + "," + load + "," + amb;
}

std::string startLine(unsigned n) {
    return "# RUN START n=" + std::to_string(n) + " cap=" + std::to_string((unsigned)kCapCounts) + " limit=75.0";
}
const char* kColumns = "# columns: k,time_s,u_counts,load_C,ambient_C";

// Loads v, then starts a run at t0 with load == ambient. Clears the sink after loading and
// leaves the RUN START / columns lines in it.
void startRun(const std::vector<unsigned>& v, uint32_t t0) {
    loadFull(v, 1000);
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(t0, R(25.0f), R(25.0f), true));
}

std::vector<unsigned> alt(uint32_t n) {
    std::vector<unsigned> v(n);
    for (uint32_t i = 0; i < n; i++) v[i] = (i % 3 == 0) ? HIGH : LOW;
    return v;
}

void expectRefusedNoRun(const std::string& expected) {
    expectOneLine(expected);
    TEST_ASSERT_FALSE(prbs->running());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
    sink.lines.clear();
    prbs->tick(50000);
    prbs->onPeriod(50000, R(25.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
}

}  // namespace

void test_AC3_refused_no_sequence() {
    TEST_ASSERT_FALSE(prbs->requestRun(5000, R(25.0f), R(25.0f), true));
    expectRefusedNoRun("REFUSED: no sequence loaded");
}

void test_AC3_no_sequence_wins_over_no_measurement_and_tolerance() {
    TEST_ASSERT_FALSE(prbs->requestRun(5000, R(60.0f), R(25.0f), false));
    expectRefusedNoRun("REFUSED: no sequence loaded");
}

void test_AC3_refused_no_measurement() {
    loadFull({LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_FALSE(prbs->requestRun(5000, R(25.0f), R(25.0f), false));
    expectRefusedNoRun("REFUSED: no measurement yet");
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_AC3_no_measurement_wins_over_tolerance() {
    loadFull({LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_FALSE(prbs->requestRun(5000, R(60.0f), R(25.0f), false));
    expectRefusedNoRun("REFUSED: no measurement yet");
}

void test_AC3_refused_load_hotter_than_ambient() {
    loadFull({LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_FALSE(prbs->requestRun(5000, R(30.0f), R(25.0f), true));
    expectRefusedNoRun("REFUSED: load must be at ambient temperature first (|load - ambient| = 5.0 C > 3.0 C)");
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_AC3_refused_load_colder_than_ambient() {
    loadFull({LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_FALSE(prbs->requestRun(5000, R(20.0f), R(26.5f), true));
    expectRefusedNoRun("REFUSED: load must be at ambient temperature first (|load - ambient| = 6.5 C > 3.0 C)");
}

void test_AC3_difference_of_exactly_3_is_accepted() {
    loadFull({LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(5000, R(28.0f), R(25.0f), true));
    TEST_ASSERT_TRUE(prbs->running());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Running);
}

void test_AC3_difference_of_exactly_minus_3_is_accepted() {
    loadFull({LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(5000, R(22.0f), R(25.0f), true));
    TEST_ASSERT_TRUE(prbs->running());
}

void test_AC3_accepted_run_prints_start_and_columns() {
    loadFull({LOW, HIGH, LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(5000, R(25.0f), R(25.0f), true));
    TEST_ASSERT_EQUAL_UINT32(2, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(startLine(4).c_str(), sink.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING(kColumns, sink.lines[1].c_str());
    TEST_ASSERT_TRUE(prbs->running());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Running);
}

void test_AC4_duty_follows_sequence_exactly_at_second_boundaries() {
    std::vector<unsigned> v = {LOW, HIGH, HIGH, LOW, LOW, HIGH};
    const uint32_t t0 = 10000;
    startRun(v, t0);
    for (uint32_t k = 0; k < v.size(); k++) {
        prbs->tick(t0 + k * 1000);
        TEST_ASSERT_EQUAL_UINT32(v[k], prbs->dutyCounts());
        prbs->tick(t0 + k * 1000 + 999);
        TEST_ASSERT_EQUAL_UINT32(v[k], prbs->dutyCounts());
        TEST_ASSERT_TRUE(prbs->running());
    }
}

void test_AC4_duty_changes_at_1000_not_999() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH}, t0);
    prbs->tick(t0 + 999);
    TEST_ASSERT_EQUAL_UINT32(LOW, prbs->dutyCounts());
    prbs->tick(t0 + 1000);
    TEST_ASSERT_EQUAL_UINT32(HIGH, prbs->dutyCounts());
}

void test_AC4_duty_is_zero_when_not_running() {
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    loadFull({LOW, HIGH}, 1000);
    prbs->tick(5000);
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
}

void test_AC5_data_line_on_time() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH, LOW}, t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.26f), R(25.04f));
    TEST_ASSERT_EQUAL_UINT32(1, sink.lines.size() - 2);
    TEST_ASSERT_EQUAL_STRING(dataLine(0, 1, LOW, "30.3", "25.0").c_str(), sink.lines[2].c_str());
    prbs->tick(t0 + 2000);
    prbs->onPeriod(t0 + 2000, R(31.0f), R(25.5f));
    TEST_ASSERT_EQUAL_UINT32(2, sink.lines.size() - 2);
    TEST_ASSERT_EQUAL_STRING(dataLine(1, 2, HIGH, "31.0", "25.5").c_str(), sink.lines[3].c_str());
}

void test_AC5_data_line_printed_late_has_larger_time() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH, LOW}, t0);
    prbs->tick(t0 + 1300);
    prbs->onPeriod(t0 + 1300, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(dataLine(0, 1, LOW, "30.0", "25.0").c_str(), sink.lines[2].c_str());
    // k=1 completes at 2000; printed late at 2400 -> time_s 2
    prbs->tick(t0 + 2400);
    prbs->onPeriod(t0 + 2400, R(31.0f), R(26.0f));
    TEST_ASSERT_EQUAL_STRING(dataLine(1, 2, HIGH, "31.0", "26.0").c_str(), sink.lines[3].c_str());
}

void test_AC5_zero_sample_result_still_prints_a_line_with_nan_at_k0() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH, LOW}, t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(0.0f, 0), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(dataLine(0, 1, LOW, "nan", "25.0").c_str(), sink.lines[2].c_str());
    prbs->tick(t0 + 2000);
    prbs->onPeriod(t0 + 2000, R(0.0f, 0), R(0.0f, 0));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(dataLine(1, 2, HIGH, "nan", "25.0").c_str(), sink.lines[3].c_str());
}

void test_AC5_zero_sample_result_reuses_previous_temperature() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH, LOW, HIGH}, t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.0f), R(25.0f));
    prbs->tick(t0 + 2000);
    prbs->onPeriod(t0 + 2000, R(99.0f, 0), R(26.0f));
    TEST_ASSERT_EQUAL_STRING(dataLine(1, 2, HIGH, "30.0", "26.0").c_str(), sink.lines[3].c_str());
    prbs->tick(t0 + 3000);
    prbs->onPeriod(t0 + 3000, R(99.0f, 0), R(99.0f, 0));
    TEST_ASSERT_EQUAL_STRING(dataLine(2, 3, LOW, "30.0", "26.0").c_str(), sink.lines[4].c_str());
}

void test_E6_stall_prints_every_missed_line_late() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    // stall: next call only at 5.2 s -> k = 1..4 pending, time_s = 5 for all
    prbs->tick(t0 + 5200);
    prbs->onPeriod(t0 + 5200, R(40.0f), R(26.0f));
    TEST_ASSERT_EQUAL_UINT32(7, sink.lines.size());
    std::vector<unsigned> v = alt(10);
    TEST_ASSERT_EQUAL_STRING(dataLine(1, 5, v[1], "40.0", "26.0").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_EQUAL_STRING(dataLine(2, 5, v[2], "40.0", "26.0").c_str(), sink.lines[4].c_str());
    TEST_ASSERT_EQUAL_STRING(dataLine(3, 5, v[3], "40.0", "26.0").c_str(), sink.lines[5].c_str());
    TEST_ASSERT_EQUAL_STRING(dataLine(4, 5, v[4], "40.0", "26.0").c_str(), sink.lines[6].c_str());
    TEST_ASSERT_EQUAL_UINT32(v[5], prbs->dutyCounts());
}

void test_E6_stall_from_start_has_no_previous_value_for_zero_sample_sensor() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 3500);
    prbs->onPeriod(t0 + 3500, R(0.0f, 0), R(27.0f));
    TEST_ASSERT_EQUAL_UINT32(5, sink.lines.size());
    std::vector<unsigned> v = alt(10);
    for (unsigned k = 0; k < 3; k++)
        TEST_ASSERT_EQUAL_STRING(dataLine(k, 3, v[k], "nan", "27.0").c_str(), sink.lines[2 + k].c_str());
}

void test_E6_stall_first_line_fresh_others_previous_from_start() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 3500);
    prbs->onPeriod(t0 + 3500, R(31.0f), R(26.0f));
    std::vector<unsigned> v = alt(10);
    TEST_ASSERT_EQUAL_UINT32(5, sink.lines.size());
    for (unsigned k = 0; k < 3; k++)
        TEST_ASSERT_EQUAL_STRING(dataLine(k, 3, v[k], "31.0", "26.0").c_str(), sink.lines[2 + k].c_str());
}

void test_E6_stall_after_zero_sample_uses_previous_line_temperature() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.0f), R(25.0f));
    prbs->tick(t0 + 4000);
    prbs->onPeriod(t0 + 4000, R(77.0f, 0), R(28.0f));  // load has no samples
    std::vector<unsigned> v = alt(10);
    TEST_ASSERT_EQUAL_UINT32(6, sink.lines.size());
    for (unsigned k = 1; k < 4; k++)
        TEST_ASSERT_EQUAL_STRING(dataLine(k, 4, v[k], "30.0", "28.0").c_str(), sink.lines[1 + k + 1].c_str());
}

void test_AC7_run_end_after_last_line_and_idle() {
    const uint32_t t0 = 10000;
    std::vector<unsigned> v = {LOW, HIGH, LOW};
    startRun(v, t0);
    for (uint32_t k = 1; k <= 3; k++) {
        prbs->tick(t0 + k * 1000 - 1);
        TEST_ASSERT_EQUAL_UINT32(v[k - 1], prbs->dutyCounts());
        TEST_ASSERT_TRUE(prbs->running());
        prbs->tick(t0 + k * 1000);
        prbs->onPeriod(t0 + k * 1000, R(30.0f), R(25.0f));
    }
    TEST_ASSERT_EQUAL_UINT32(6, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(dataLine(2, 3, LOW, "30.0", "25.0").c_str(), sink.lines[4].c_str());
    TEST_ASSERT_EQUAL_STRING("# RUN END ok n=3", sink.lines[5].c_str());
    TEST_ASSERT_FALSE(prbs->running());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->hasSequence());
    sink.lines.clear();
    prbs->tick(t0 + 9000);
    prbs->onPeriod(t0 + 9000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
}

void test_AC7_duty_is_zero_once_last_second_is_over_even_before_onperiod() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH}, t0);
    prbs->tick(t0 + 2000);
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
}

void test_AC7_run_end_after_a_stall_on_last_call() {
    const uint32_t t0 = 10000;
    std::vector<unsigned> v = {LOW, HIGH, LOW};
    startRun(v, t0);
    prbs->tick(t0 + 10000);
    prbs->onPeriod(t0 + 10000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(6, sink.lines.size());
    for (unsigned k = 0; k < 3; k++)
        TEST_ASSERT_EQUAL_STRING(dataLine(k, 10, v[k], "30.0", "25.0").c_str(), sink.lines[2 + k].c_str());
    TEST_ASSERT_EQUAL_STRING("# RUN END ok n=3", sink.lines[5].c_str());
    TEST_ASSERT_FALSE(prbs->running());
}

void test_AC7_new_run_and_new_load_possible_after_end() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH}, t0);
    prbs->tick(t0 + 2000);
    prbs->onPeriod(t0 + 2000, R(30.0f), R(25.0f));
    TEST_ASSERT_FALSE(prbs->running());
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(t0 + 5000, R(25.0f), R(25.0f), true));
    TEST_ASSERT_EQUAL_STRING(startLine(2).c_str(), sink.lines[0].c_str());
    prbs->tick(t0 + 5000);
    TEST_ASSERT_EQUAL_UINT32(LOW, prbs->dutyCounts());
    prbs->tick(t0 + 7000);
    prbs->onPeriod(t0 + 7000, R(30.0f), R(25.0f));
    TEST_ASSERT_FALSE(prbs->running());
    TEST_ASSERT_TRUE(prbs->beginLoad(3, t0 + 8000));
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Loading);
}

void test_AC11_pace_depends_on_clock_not_on_call_count() {
    const uint32_t t0 = 10000;
    std::vector<unsigned> v = alt(20);
    startRun(v, t0);
    prbs->onPeriod(t0 + 500, R(30.0f), R(25.0f));  // nothing completed yet
    TEST_ASSERT_EQUAL_UINT32(2, sink.lines.size());
    prbs->tick(t0 + 5200);
    prbs->onPeriod(t0 + 5200, R(30.0f), R(25.0f));  // one call, five lines
    TEST_ASSERT_EQUAL_UINT32(7, sink.lines.size());
    for (unsigned k = 0; k < 5; k++)
        TEST_ASSERT_EQUAL_STRING(dataLine(k, 5, v[k], "30.0", "25.0").c_str(), sink.lines[2 + k].c_str());
    prbs->onPeriod(t0 + 5200, R(31.0f), R(25.0f));  // same second again: nothing
    prbs->onPeriod(t0 + 5900, R(31.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(7, sink.lines.size());
    prbs->tick(t0 + 6000);
    prbs->onPeriod(t0 + 6000, R(31.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(8, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(dataLine(5, 6, v[5], "31.0", "25.0").c_str(), sink.lines[7].c_str());
}

void test_AC11_many_calls_in_the_same_second_do_not_advance_the_run() {
    const uint32_t t0 = 10000;
    startRun(alt(5), t0);
    for (int i = 0; i < 10; i++) prbs->onPeriod(t0 + 100 + i, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(2, sink.lines.size());
    prbs->tick(t0 + 999);
    TEST_ASSERT_EQUAL_UINT32(alt(5)[0], prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->running());
}

void test_AC10_no_duty_or_printed_value_above_cap_in_a_whole_run() {
    const uint32_t t0 = 10000;
    std::vector<unsigned> v(60);
    for (unsigned i = 0; i < 60; i++) v[i] = (i % 2) ? kCapCounts : LOW;
    startRun(v, t0);
    for (uint32_t ms = 0; ms <= 61000; ms += 250) {
        prbs->tick(t0 + ms);
        TEST_ASSERT_TRUE(prbs->dutyCounts() <= kCapCounts);
        prbs->onPeriod(t0 + ms, R(40.0f), R(25.0f));
    }
    TEST_ASSERT_EQUAL_UINT32(2 + 60 + 1, sink.lines.size());
    for (unsigned k = 0; k < 60; k++) {
        const std::string& l = sink.lines[2 + k];
        size_t c1 = l.find(',');
        size_t c2 = l.find(',', c1 + 1);
        size_t c3 = l.find(',', c2 + 1);
        unsigned u = (unsigned)std::stoul(l.substr(c2 + 1, c3 - c2 - 1));
        TEST_ASSERT_TRUE(u <= kCapCounts);
        TEST_ASSERT_EQUAL_UINT32(v[k], u);
    }
    TEST_ASSERT_EQUAL_STRING("# RUN END ok n=60", sink.lines.back().c_str());
}

void test_D2_max_length_sequence_is_played_back_bit_for_bit() {
    const uint32_t N = prbscfg::kMaxValues;
    std::vector<unsigned> v(N);
    uint32_t s = 0x1234ABCDu;
    for (uint32_t i = 0; i < N; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;  // xorshift32, fixed seed
        v[i] = (s & 0x100u) ? HIGH : LOW;
    }
    TEST_ASSERT_TRUE(prbs->beginLoad(N, 0));
    for (uint32_t i = 0; i < N; i++) feed(val(v[i]), 100);
    prbs->tick(100 + prbscfg::kLoadTailMs);
    TEST_ASSERT_TRUE(prbs->hasSequence());
    sink.lines.clear();
    const uint32_t t0 = 20000;
    TEST_ASSERT_TRUE(prbs->requestRun(t0, R(25.0f), R(25.0f), true));
    for (uint32_t k = 0; k < N; k++) {
        prbs->tick(t0 + k * 1000);
        TEST_ASSERT_EQUAL_UINT32(v[k], prbs->dutyCounts());
        if (k > 0) prbs->onPeriod(t0 + k * 1000, R(30.0f), R(25.0f));
    }
    prbs->tick(t0 + N * 1000);
    prbs->onPeriod(t0 + N * 1000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(2 + N + 1, sink.lines.size());
    for (uint32_t k = 0; k < N; k++) {
        char buf[100];
        snprintf(buf, sizeof buf, "%u,%u,%u,30.0,25.0", (unsigned)k, (unsigned)(k + 1), v[k]);
        TEST_ASSERT_EQUAL_STRING(buf, sink.lines[2 + k].c_str());
    }
    TEST_ASSERT_EQUAL_STRING("# RUN END ok n=32000", sink.lines.back().c_str());
    TEST_ASSERT_FALSE(prbs->running());
}

// ---------------------------------------------------------------- T11: safety, stop, latch

namespace {

PeriodResult P(float t, float mean, uint16_t samples = 10) {
    PeriodResult r;
    r.tempC = t;
    r.meanCount = mean;
    r.samples = samples;
    return r;
}

const char* kRefusedLatch = "REFUSED: reset required after cutoff";

std::string abortLine(unsigned k, const char* reason) {
    return "# RUN END aborted k=" + std::to_string(k) + " reason=" + reason;
}

// Starts a run of alt(10) at t0, completes second 0 normally (one data line printed), then at
// t0 + 1000 delivers `load`/`ambient`. Leaves the sink holding START, columns, line 0.
void runThenSecond(const PeriodResult& load, const PeriodResult& amb, uint32_t t0 = 10000) {
    startRun(alt(10), t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    prbs->tick(t0 + 2000);
    TEST_ASSERT_TRUE(prbs->dutyCounts() != 0);
    prbs->onPeriod(t0 + 2000, load, amb);
}

}  // namespace

void test_AC8_sensor_fault_load_count_0() {
    runThenSecond(P(30.0f, 0.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "sensor fault load").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_FALSE(prbs->running());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
    TEST_ASSERT_TRUE(prbs->latched());
}

void test_AC8_sensor_fault_load_count_4095() {
    runThenSecond(P(30.0f, 4095.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "sensor fault load").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
}

void test_AC8_sensor_fault_ambient_count_0() {
    runThenSecond(R(30.0f), P(25.0f, 0.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "sensor fault ambient").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
    TEST_ASSERT_TRUE(prbs->latched());
}

void test_AC8_sensor_fault_ambient_count_4095() {
    runThenSecond(R(30.0f), P(25.0f, 4095.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "sensor fault ambient").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
}

void test_AC8_over_temperature_exactly_75() {
    runThenSecond(R(75.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "over-temperature").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_FALSE(prbs->running());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
    TEST_ASSERT_TRUE(prbs->latched());
}

void test_AC8_over_temperature_well_above_75() {
    runThenSecond(R(90.0f), R(25.0f));
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "over-temperature").c_str(), sink.lines[3].c_str());
}

void test_AC8_temperature_74_9_does_not_abort() {
    runThenSecond(R(74.9f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(dataLine(1, 2, alt(10)[1], "74.9", "25.0").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_TRUE(prbs->running());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Running);
    TEST_ASSERT_FALSE(prbs->latched());
    TEST_ASSERT_EQUAL_UINT32(alt(10)[2], (prbs->tick(12000), prbs->dutyCounts()));
}

void test_AC8_ambient_temperature_has_no_limit() {
    runThenSecond(R(30.0f), R(90.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(dataLine(1, 2, alt(10)[1], "30.0", "90.0").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_TRUE(prbs->running());
    TEST_ASSERT_FALSE(prbs->latched());
}

void test_AC8_load_fault_wins_over_ambient_fault_and_heat() {
    runThenSecond(P(80.0f, 0.0f), P(25.0f, 4095.0f));
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "sensor fault load").c_str(), sink.lines[3].c_str());
}

void test_AC8_ambient_fault_wins_over_heat() {
    runThenSecond(R(80.0f), P(25.0f, 0.0f));
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "sensor fault ambient").c_str(), sink.lines[3].c_str());
}

void test_AC8_zero_sample_results_are_never_a_fault() {
    runThenSecond(P(90.0f, 0.0f, 0), P(25.0f, 4095.0f, 0));
    TEST_ASSERT_TRUE(prbs->running());
    TEST_ASSERT_FALSE(prbs->latched());
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());  // a (nan/held) line is printed, no abort
    TEST_ASSERT_TRUE(sink.lines[3].find("RUN END") == std::string::npos);
}

void test_AC8_zero_sample_ambient_only_does_not_hide_a_load_fault() {
    runThenSecond(P(30.0f, 0.0f), P(25.0f, 0.0f, 0));
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "sensor fault load").c_str(), sink.lines[3].c_str());
}

void test_AC8_k_is_zero_when_the_first_second_faults() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(76.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());  // START, columns, abort
    TEST_ASSERT_EQUAL_STRING(abortLine(0, "over-temperature").c_str(), sink.lines[2].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
}

void test_AC8_k_counts_all_printed_lines() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    for (uint32_t k = 1; k <= 4; k++) {
        prbs->tick(t0 + k * 1000);
        prbs->onPeriod(t0 + k * 1000, R(30.0f), R(25.0f));
    }
    TEST_ASSERT_EQUAL_UINT32(6, sink.lines.size());
    prbs->tick(t0 + 5000);
    prbs->onPeriod(t0 + 5000, P(30.0f, 4095.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(7, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(4, "sensor fault load").c_str(), sink.lines[6].c_str());
}

void test_AC8_check_runs_before_catch_up_lines_after_a_stall() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    // stall: seconds 1..4 pending; the faulty result belongs to the oldest one (k=1)
    prbs->tick(t0 + 5200);
    prbs->onPeriod(t0 + 5200, R(80.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "over-temperature").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
}

void test_AC8_fault_in_the_period_that_would_end_the_run_still_aborts() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH, LOW}, t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.0f), R(25.0f));
    prbs->tick(t0 + 2000);
    prbs->onPeriod(t0 + 2000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    prbs->tick(t0 + 3000);
    prbs->onPeriod(t0 + 3000, R(75.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(5, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(2, "over-temperature").c_str(), sink.lines[4].c_str());
    for (const std::string& l : sink.lines) TEST_ASSERT_TRUE(l.find("RUN END ok") == std::string::npos);
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
}

void test_AC8_fault_in_a_long_stall_that_would_end_the_run_still_aborts() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH, LOW}, t0);
    prbs->tick(t0 + 10000);
    prbs->onPeriod(t0 + 10000, P(30.0f, 0.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(0, "sensor fault load").c_str(), sink.lines[2].c_str());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
}

void test_AC8_after_abort_tick_and_onperiod_print_nothing_and_duty_stays_zero() {
    const uint32_t t0 = 10000;
    runThenSecond(R(80.0f), R(25.0f), t0);
    size_t n = sink.lines.size();
    for (uint32_t ms = 2100; ms < 15000; ms += 400) {
        prbs->tick(t0 + ms);
        TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
        prbs->onPeriod(t0 + ms, R(30.0f), R(25.0f));
    }
    TEST_ASSERT_EQUAL_UINT32(n, sink.lines.size());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
}

void test_E3_run_refused_after_latch() {
    const uint32_t t0 = 10000;
    runThenSecond(P(30.0f, 4095.0f), R(25.0f), t0);
    sink.lines.clear();
    TEST_ASSERT_FALSE(prbs->requestRun(t0 + 8000, R(25.0f), R(25.0f), true));
    expectOneLine(kRefusedLatch);
    TEST_ASSERT_FALSE(prbs->running());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
    sink.lines.clear();
    prbs->tick(t0 + 9000);
    prbs->onPeriod(t0 + 9000, R(25.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
}

void test_E3_latch_refusal_comes_before_every_other_refusal() {
    const uint32_t t0 = 10000;
    runThenSecond(R(80.0f), R(25.0f), t0);
    sink.lines.clear();
    // not at ambient and no measurement: the latch text still wins
    TEST_ASSERT_FALSE(prbs->requestRun(t0 + 8000, R(60.0f), R(25.0f), false));
    expectOneLine(kRefusedLatch);
}

void test_E3_repeated_refusals_keep_refusing() {
    runThenSecond(R(80.0f), P(25.0f, 0.0f));
    for (int i = 0; i < 3; i++) {
        sink.lines.clear();
        TEST_ASSERT_FALSE(prbs->requestRun(30000 + i, R(25.0f), R(25.0f), true));
        expectOneLine(kRefusedLatch);
        TEST_ASSERT_TRUE(prbs->latched());
    }
}

void test_E3_fresh_object_is_not_latched() {
    TEST_ASSERT_FALSE(prbs->latched());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
    runThenSecond(R(80.0f), R(25.0f));
    TEST_ASSERT_TRUE(prbs->latched());
    setUp();  // a board reset: new object
    TEST_ASSERT_FALSE(prbs->latched());
    loadFull({LOW, HIGH}, 1000);
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(5000, R(25.0f), R(25.0f), true));
}

void test_E3_normal_end_does_not_latch() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH}, t0);
    prbs->tick(t0 + 2000);
    prbs->onPeriod(t0 + 2000, R(30.0f), R(25.0f));
    TEST_ASSERT_FALSE(prbs->latched());
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(t0 + 5000, R(25.0f), R(25.0f), true));
}

void test_AC9_stop_during_run() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 1000);
    prbs->onPeriod(t0 + 1000, R(30.0f), R(25.0f));
    prbs->tick(t0 + 1600);
    TEST_ASSERT_TRUE(prbs->dutyCounts() != 0);
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    prbs->stop(t0 + 1600);
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_EQUAL_UINT32(4, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(1, "stopped by user").c_str(), sink.lines[3].c_str());
    TEST_ASSERT_FALSE(prbs->running());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
    TEST_ASSERT_FALSE(prbs->latched());
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_AC9_stop_in_first_second_has_k_zero() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 300);
    prbs->stop(t0 + 300);
    TEST_ASSERT_EQUAL_UINT32(3, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING(abortLine(0, "stopped by user").c_str(), sink.lines[2].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
}

void test_AC9_nothing_is_printed_or_driven_after_stop() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 500);
    prbs->stop(t0 + 500);
    size_t n = sink.lines.size();
    for (uint32_t ms = 600; ms < 12000; ms += 300) {
        prbs->tick(t0 + ms);
        TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
        prbs->onPeriod(t0 + ms, R(30.0f), R(25.0f));
    }
    TEST_ASSERT_EQUAL_UINT32(n, sink.lines.size());
}

void test_AC9_new_run_possible_after_stop() {
    const uint32_t t0 = 10000;
    startRun(alt(10), t0);
    prbs->tick(t0 + 1500);
    prbs->stop(t0 + 1500);
    sink.lines.clear();
    TEST_ASSERT_TRUE(prbs->requestRun(t0 + 3000, R(25.0f), R(25.0f), true));
    TEST_ASSERT_EQUAL_STRING(startLine(10).c_str(), sink.lines[0].c_str());
    prbs->tick(t0 + 3000);
    TEST_ASSERT_EQUAL_UINT32(alt(10)[0], prbs->dutyCounts());
    prbs->tick(t0 + 4000);
    prbs->onPeriod(t0 + 4000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_STRING(dataLine(0, 1, alt(10)[0], "30.0", "25.0").c_str(), sink.lines[2].c_str());
}

void test_AC9_stop_when_idle_prints_nothing() {
    prbs->stop(1000);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
    TEST_ASSERT_FALSE(prbs->latched());
    loadFull({LOW, HIGH}, 2000);
    sink.lines.clear();
    prbs->stop(3000);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_TRUE(prbs->hasSequence());
}

void test_AC9_stop_after_normal_end_prints_nothing() {
    const uint32_t t0 = 10000;
    startRun({LOW, HIGH}, t0);
    prbs->tick(t0 + 2000);
    prbs->onPeriod(t0 + 2000, R(30.0f), R(25.0f));
    sink.lines.clear();
    prbs->stop(t0 + 3000);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Idle);
}

void test_AC9_stop_after_latch_changes_nothing() {
    runThenSecond(R(80.0f), R(25.0f));
    sink.lines.clear();
    prbs->stop(20000);
    TEST_ASSERT_EQUAL_UINT32(0, sink.lines.size());
    TEST_ASSERT_TRUE(prbs->latched());
    TEST_ASSERT_TRUE(prbs->state() == PrbsState::Latched);
}

void test_AC10_cap_respected_in_aborted_and_stopped_runs() {
    const uint32_t t0 = 10000;
    std::vector<unsigned> v(20);
    for (unsigned i = 0; i < 20; i++) v[i] = (i % 2) ? kCapCounts : LOW;
    // aborted run
    startRun(v, t0);
    for (uint32_t k = 1; k <= 8; k++) {
        prbs->tick(t0 + k * 1000);
        TEST_ASSERT_TRUE(prbs->dutyCounts() <= kCapCounts);
        prbs->onPeriod(t0 + k * 1000, k == 8 ? R(80.0f) : R(40.0f), R(25.0f));
    }
    TEST_ASSERT_TRUE(prbs->latched());
    TEST_ASSERT_EQUAL_STRING(abortLine(7, "over-temperature").c_str(), sink.lines.back().c_str());
    for (size_t i = 2; i + 1 < sink.lines.size(); i++) {
        const std::string& l = sink.lines[i];
        size_t c1 = l.find(',');
        size_t c2 = l.find(',', c1 + 1);
        size_t c3 = l.find(',', c2 + 1);
        TEST_ASSERT_TRUE((unsigned)std::stoul(l.substr(c2 + 1, c3 - c2 - 1)) <= kCapCounts);
    }
    // stopped run
    setUp();
    startRun(v, t0);
    for (uint32_t k = 1; k <= 5; k++) {
        prbs->tick(t0 + k * 1000);
        prbs->onPeriod(t0 + k * 1000, R(40.0f), R(25.0f));
    }
    prbs->tick(t0 + 5500);
    TEST_ASSERT_TRUE(prbs->dutyCounts() <= kCapCounts);
    prbs->stop(t0 + 5500);
    TEST_ASSERT_EQUAL_UINT32(0, prbs->dutyCounts());
    TEST_ASSERT_EQUAL_STRING(abortLine(5, "stopped by user").c_str(), sink.lines.back().c_str());
    for (size_t i = 2; i + 1 < sink.lines.size(); i++) {
        const std::string& l = sink.lines[i];
        size_t c1 = l.find(',');
        size_t c2 = l.find(',', c1 + 1);
        size_t c3 = l.find(',', c2 + 1);
        TEST_ASSERT_TRUE((unsigned)std::stoul(l.substr(c2 + 1, c3 - c2 - 1)) <= kCapCounts);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_AC1_begin_load_prints_ready);
    RUN_TEST(test_AC2_valid_sequence_levels_and_changes);
    RUN_TEST(test_AC2_order_of_levels_does_not_matter);
    RUN_TEST(test_E5_all_equal_sequence);
    RUN_TEST(test_E5_all_zero_sequence);
    RUN_TEST(test_E5_zero_as_one_of_two_levels);
    RUN_TEST(test_E5_single_value_sequence);
    RUN_TEST(test_AC2_cr_and_blanks_are_tolerated);
    RUN_TEST(test_value_equal_to_cap_is_accepted);
    RUN_TEST(test_E12_bad_length_zero);
    RUN_TEST(test_E12_bad_length_too_large);
    RUN_TEST(test_E12_bad_length_clears_earlier_sequence);
    RUN_TEST(test_E12_bad_length_accepts_no_value);
    RUN_TEST(test_AC2_max_length_is_accepted);
    RUN_TEST(test_E8_begin_load_discards_earlier_sequence);
    RUN_TEST(test_E11_text_line);
    RUN_TEST(test_E11_decimal_point);
    RUN_TEST(test_E11_trailing_decimal_point);
    RUN_TEST(test_E11_exponent);
    RUN_TEST(test_E11_minus_sign);
    RUN_TEST(test_E11_plus_sign);
    RUN_TEST(test_E11_command_line);
    RUN_TEST(test_E11_blank_line);
    RUN_TEST(test_E11_blank_spaces_line);
    RUN_TEST(test_E11_embedded_space);
    RUN_TEST(test_E11_line_longer_than_31_chars);
    RUN_TEST(test_E11_cr_only_blank_line);
    RUN_TEST(test_E11_refusal_clears_earlier_sequence);
    RUN_TEST(test_E11_characters_after_refusal_are_ignored);
    RUN_TEST(test_AC2_value_above_cap);
    RUN_TEST(test_AC2_value_above_cap_clears_earlier_sequence);
    RUN_TEST(test_AC2_value_with_more_than_five_digits_is_above_cap);
    RUN_TEST(test_AC2_third_distinct_value);
    RUN_TEST(test_AC2_third_distinct_value_clears_earlier_sequence);
    RUN_TEST(test_E7_timeout_before_any_value);
    RUN_TEST(test_E7_timeout_counts_accepted_values);
    RUN_TEST(test_E7_partial_characters_do_not_reset_timer);
    RUN_TEST(test_E7_timeout_clears_earlier_sequence);
    RUN_TEST(test_E7_tick_when_idle_prints_nothing);
    RUN_TEST(test_AC2_extra_character_in_quiet_window);
    RUN_TEST(test_AC2_extra_newline_in_quiet_window);
    RUN_TEST(test_AC2_extra_character_clears_earlier_sequence_and_stores_nothing);
    RUN_TEST(test_E8_second_load_replaces_first);
    RUN_TEST(test_D2_max_length_pseudo_random_sequence);
    RUN_TEST(test_AC3_refused_no_sequence);
    RUN_TEST(test_AC3_no_sequence_wins_over_no_measurement_and_tolerance);
    RUN_TEST(test_AC3_refused_no_measurement);
    RUN_TEST(test_AC3_no_measurement_wins_over_tolerance);
    RUN_TEST(test_AC3_refused_load_hotter_than_ambient);
    RUN_TEST(test_AC3_refused_load_colder_than_ambient);
    RUN_TEST(test_AC3_difference_of_exactly_3_is_accepted);
    RUN_TEST(test_AC3_difference_of_exactly_minus_3_is_accepted);
    RUN_TEST(test_AC3_accepted_run_prints_start_and_columns);
    RUN_TEST(test_AC4_duty_follows_sequence_exactly_at_second_boundaries);
    RUN_TEST(test_AC4_duty_changes_at_1000_not_999);
    RUN_TEST(test_AC4_duty_is_zero_when_not_running);
    RUN_TEST(test_AC5_data_line_on_time);
    RUN_TEST(test_AC5_data_line_printed_late_has_larger_time);
    RUN_TEST(test_AC5_zero_sample_result_still_prints_a_line_with_nan_at_k0);
    RUN_TEST(test_AC5_zero_sample_result_reuses_previous_temperature);
    RUN_TEST(test_E6_stall_prints_every_missed_line_late);
    RUN_TEST(test_E6_stall_from_start_has_no_previous_value_for_zero_sample_sensor);
    RUN_TEST(test_E6_stall_first_line_fresh_others_previous_from_start);
    RUN_TEST(test_E6_stall_after_zero_sample_uses_previous_line_temperature);
    RUN_TEST(test_AC7_run_end_after_last_line_and_idle);
    RUN_TEST(test_AC7_duty_is_zero_once_last_second_is_over_even_before_onperiod);
    RUN_TEST(test_AC7_run_end_after_a_stall_on_last_call);
    RUN_TEST(test_AC7_new_run_and_new_load_possible_after_end);
    RUN_TEST(test_AC11_pace_depends_on_clock_not_on_call_count);
    RUN_TEST(test_AC11_many_calls_in_the_same_second_do_not_advance_the_run);
    RUN_TEST(test_AC10_no_duty_or_printed_value_above_cap_in_a_whole_run);
    RUN_TEST(test_D2_max_length_sequence_is_played_back_bit_for_bit);
    RUN_TEST(test_AC8_sensor_fault_load_count_0);
    RUN_TEST(test_AC8_sensor_fault_load_count_4095);
    RUN_TEST(test_AC8_sensor_fault_ambient_count_0);
    RUN_TEST(test_AC8_sensor_fault_ambient_count_4095);
    RUN_TEST(test_AC8_over_temperature_exactly_75);
    RUN_TEST(test_AC8_over_temperature_well_above_75);
    RUN_TEST(test_AC8_temperature_74_9_does_not_abort);
    RUN_TEST(test_AC8_ambient_temperature_has_no_limit);
    RUN_TEST(test_AC8_load_fault_wins_over_ambient_fault_and_heat);
    RUN_TEST(test_AC8_ambient_fault_wins_over_heat);
    RUN_TEST(test_AC8_zero_sample_results_are_never_a_fault);
    RUN_TEST(test_AC8_zero_sample_ambient_only_does_not_hide_a_load_fault);
    RUN_TEST(test_AC8_k_is_zero_when_the_first_second_faults);
    RUN_TEST(test_AC8_k_counts_all_printed_lines);
    RUN_TEST(test_AC8_check_runs_before_catch_up_lines_after_a_stall);
    RUN_TEST(test_AC8_fault_in_the_period_that_would_end_the_run_still_aborts);
    RUN_TEST(test_AC8_fault_in_a_long_stall_that_would_end_the_run_still_aborts);
    RUN_TEST(test_AC8_after_abort_tick_and_onperiod_print_nothing_and_duty_stays_zero);
    RUN_TEST(test_E3_run_refused_after_latch);
    RUN_TEST(test_E3_latch_refusal_comes_before_every_other_refusal);
    RUN_TEST(test_E3_repeated_refusals_keep_refusing);
    RUN_TEST(test_E3_fresh_object_is_not_latched);
    RUN_TEST(test_E3_normal_end_does_not_latch);
    RUN_TEST(test_AC9_stop_during_run);
    RUN_TEST(test_AC9_stop_in_first_second_has_k_zero);
    RUN_TEST(test_AC9_nothing_is_printed_or_driven_after_stop);
    RUN_TEST(test_AC9_new_run_possible_after_stop);
    RUN_TEST(test_AC9_stop_when_idle_prints_nothing);
    RUN_TEST(test_AC9_stop_after_normal_end_prints_nothing);
    RUN_TEST(test_AC9_stop_after_latch_changes_nothing);
    RUN_TEST(test_AC10_cap_respected_in_aborted_and_stopped_runs);
    return UNITY_END();
}
