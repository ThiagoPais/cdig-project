#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "measure.h"
#include "sweep.h"
#include "commands.h"
#include "duty.h"
#include "PrbsConfig.h"
#include "prbs.h"
#include "supervisor.h"

namespace {

typedef std::vector<std::string> Lines;

struct RecSink : LineSink {
    Lines lines;
    void line(const char* text) override { lines.push_back(text); }
};

RecSink sink;
Supervisor* sup = nullptr;

const unsigned LOW = kCapCounts / 3;
const unsigned HIGH = kCapCounts / 2;

PeriodResult R(float t, uint16_t samples = 10) {
    PeriodResult r;
    r.tempC = t;
    r.meanCount = 700.0f;
    r.samples = samples;
    return r;
}

PeriodResult P(float t, float mean, uint16_t samples = 10) {
    PeriodResult r;
    r.tempC = t;
    r.meanCount = mean;
    r.samples = samples;
    return r;
}

bool has(const Lines& l, const std::string& sub) {
    for (const auto& s : l)
        if (s.find(sub) != std::string::npos) return true;
    return false;
}

bool hasExact(const Lines& l, const std::string& text) {
    for (const auto& s : l)
        if (s == text) return true;
    return false;
}

// Feeds a text one character at a time; returns the lines printed meanwhile.
Lines send(const std::string& text, uint32_t t) {
    size_t from = sink.lines.size();
    for (char c : text) sup->onChar(c, t);
    return Lines(sink.lines.begin() + from, sink.lines.end());
}

Lines sendTick(uint32_t t) {
    size_t from = sink.lines.size();
    sup->tick(t);
    return Lines(sink.lines.begin() + from, sink.lines.end());
}

Lines sendPeriod(uint32_t t, const PeriodResult& load, const PeriodResult& amb) {
    size_t from = sink.lines.size();
    sup->onPeriod(t, load, amb);
    return Lines(sink.lines.begin() + from, sink.lines.end());
}

std::string val(unsigned v) { return std::to_string(v) + "\n"; }

// One period with both sensors at 25 C.
void measure(uint32_t t = 1000) { sendPeriod(t, R(25.0f), R(25.0f)); }

// Loads v through onChar + tick; returns all lines printed.
Lines loadSeq(const std::vector<unsigned>& v, uint32_t t) {
    size_t from = sink.lines.size();
    for (char c : "prbs load " + std::to_string(v.size()) + "\n") sup->onChar(c, t);
    for (unsigned x : v)
        for (char c : val(x)) sup->onChar(c, t);
    sup->tick(t + prbscfg::kLoadTailMs);
    return Lines(sink.lines.begin() + from, sink.lines.end());
}

std::string startLine(unsigned n) {
    return "# RUN START n=" + std::to_string(n) + " cap=" + std::to_string((unsigned)kCapCounts) + " limit=75.0";
}
const char* kColumns = "# columns: k,time_s,u_counts,load_C,ambient_C";
const char* kRunBusy = "REFUSED: a PRBS run is in progress";
const char* kSweepBusy = "REFUSED: a duty sweep is in progress";
const char* kLatch = "REFUSED: reset required after cutoff";

// measure(), load v, accept a run at t0. Leaves the sink as is.
Lines startRun(const std::vector<unsigned>& v, uint32_t t0) {
    measure(1000);
    loadSeq(v, 2000);
    return send("prbs run\n", t0);
}

// Run of {LOW,HIGH} started at t0, aborted by over-temperature at t0+1000.
void abortRun(uint32_t t0) {
    startRun({LOW, HIGH, LOW}, t0);
    sendTick(t0 + 1000);
    sendPeriod(t0 + 1000, R(75.0f), R(25.0f));
}

}  // namespace

void setUp() {
    sink.lines.clear();
    delete sup;
    sup = new Supervisor(sink);
}
void tearDown() {}

// ---------------------------------------------------------------- ready lines

void test_begin_prints_ready_and_commands_lines() {
    sup->begin(0);
    TEST_ASSERT_TRUE(has(sink.lines, "READY"));
    bool found = false;
    for (const auto& s : sink.lines) {
        const std::string head = "commands: start [N] (N = 0..";
        const std::string tail = " %), stop, results, temp, prbs load N, prbs run";
        if (s.compare(0, head.size(), head) == 0 && s.size() >= tail.size() &&
            s.compare(s.size() - tail.size(), tail.size(), tail) == 0)
            found = true;
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(has(sink.lines, "0.5"));
    TEST_ASSERT_TRUE(has(sink.lines, "120"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
}

// ---------------------------------------------------------------- load

void test_AC1_full_load_through_onChar_and_tick() {
    sup->begin(0);
    sink.lines.clear();
    Lines out = send("prbs load 3\n", 1000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("PRBS LOAD READY n=3", out[0].c_str());
    out = send(val(LOW) + val(HIGH) + val(HIGH), 1010);
    TEST_ASSERT_EQUAL_UINT32(0, out.size());
    out = sendTick(1010 + prbscfg::kLoadTailMs - 1);
    TEST_ASSERT_EQUAL_UINT32(0, out.size());
    out = sendTick(1010 + prbscfg::kLoadTailMs);
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    char buf[100];
    snprintf(buf, sizeof buf, "PRBS LEVELS low=%u high=%u changes=1", LOW, HIGH);
    TEST_ASSERT_EQUAL_STRING(buf, out[0].c_str());
    TEST_ASSERT_EQUAL_STRING("PRBS LOADED n=3", out[1].c_str());
}

void test_load_characters_do_not_reach_the_command_parser() {
    // "5" lines would be unknown commands if they reached the LineBuffer
    sup->begin(0);
    sink.lines.clear();
    Lines out = loadSeq({LOW, HIGH}, 1000);
    TEST_ASSERT_FALSE(has(out, "UNKNOWN COMMAND"));
    TEST_ASSERT_TRUE(hasExact(out, "PRBS LOADED n=2"));
}

void test_E11_stop_inside_load_stream_refuses_load_and_returns_to_command_mode() {
    sup->begin(0);
    send("prbs load 4\n", 1000);
    send(val(LOW), 1000);
    sink.lines.clear();
    Lines out = send("stop\n", 1010);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: value 2 is not a non-negative integer", out[0].c_str());
    // back in command mode: a sweep command is answered
    out = send("stop\n", 1020);
    TEST_ASSERT_TRUE(has(out, "NOTHING RUNNING"));
    out = send("temp\n", 1030);
    TEST_ASSERT_TRUE(has(out, "TEMP"));
    // the load was discarded
    measure(2000);
    out = send("prbs run\n", 3000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: no sequence loaded", out[0].c_str());
}

void test_E7_load_timeout_through_tick() {
    sup->begin(0);
    send("prbs load 3\n", 1000);
    sink.lines.clear();
    Lines out = sendTick(1000 + prbscfg::kLoadTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: no data for 5 s (got 0 of 3 values)", out[0].c_str());
}

void test_E12_bad_length_through_command() {
    sup->begin(0);
    sink.lines.clear();
    Lines out = send("prbs load 0\n", 1000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: bad length 0 (must be 1..32000)", out[0].c_str());
}

// ---------------------------------------------------------------- prbs run

void test_AC3_run_refused_without_sequence() {
    sup->begin(0);
    measure();
    sink.lines.clear();
    Lines out = send("prbs run\n", 2000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: no sequence loaded", out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC3_run_refused_before_any_period() {
    sup->begin(0);
    loadSeq({LOW, HIGH}, 1000);
    sink.lines.clear();
    Lines out = send("prbs run\n", 2000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: no measurement yet", out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC3_zero_sample_period_is_not_a_measurement() {
    sup->begin(0);
    loadSeq({LOW, HIGH}, 1000);
    sendPeriod(1500, R(25.0f), R(25.0f, 0));
    sink.lines.clear();
    Lines out = send("prbs run\n", 2000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: no measurement yet", out[0].c_str());
}

void test_AC3_run_refused_load_not_at_ambient_then_accepted_later() {
    sup->begin(0);
    loadSeq({LOW, HIGH}, 1000);
    sendPeriod(1500, R(30.0f), R(25.0f));
    sink.lines.clear();
    Lines out = send("prbs run\n", 2000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: load must be at ambient temperature first (|load - ambient| = 5.0 C > 3.0 C)",
                             out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    // the stored sequence is still usable
    sendPeriod(2500, R(25.0f), R(25.0f));
    out = send("prbs run\n", 3000);
    TEST_ASSERT_TRUE(hasExact(out, startLine(2)));
}

void test_AC3_run_accepted_prints_start_and_columns() {
    sup->begin(0);
    sink.lines.clear();
    Lines out = startRun({LOW, HIGH, LOW}, 10000);
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    TEST_ASSERT_EQUAL_STRING(startLine(3).c_str(), out[0].c_str());
    TEST_ASSERT_EQUAL_STRING(kColumns, out[1].c_str());
}

void test_D5_measurement_restart_flag_is_one_shot() {
    sup->begin(0);
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    startRun({LOW, HIGH}, 10000);
    TEST_ASSERT_TRUE(sup->takeMeasurementRestart());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
}

void test_AC4_duty_is_the_runs_duty_while_running() {
    sup->begin(0);
    startRun({LOW, HIGH, LOW}, 10000);
    sendTick(10000);
    TEST_ASSERT_EQUAL_UINT16(LOW, sup->dutyCounts());
    sendTick(10999);
    TEST_ASSERT_EQUAL_UINT16(LOW, sup->dutyCounts());
    sendTick(11000);
    TEST_ASSERT_EQUAL_UINT16(HIGH, sup->dutyCounts());
    TEST_ASSERT_TRUE(sup->dutyCounts() <= kCapCounts);
}

void test_AC5_run_data_lines_and_end_through_supervisor() {
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    sendTick(11000);
    Lines out = sendPeriod(11000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    char buf[100];
    snprintf(buf, sizeof buf, "0,1,%u,30.0,25.0", LOW);
    TEST_ASSERT_EQUAL_STRING(buf, out[0].c_str());
    sendTick(12000);
    out = sendPeriod(12000, R(31.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    snprintf(buf, sizeof buf, "1,2,%u,31.0,25.0", HIGH);
    TEST_ASSERT_EQUAL_STRING(buf, out[0].c_str());
    TEST_ASSERT_EQUAL_STRING("# RUN END ok n=2", out[1].c_str());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

// ---------------------------------------------------------------- precedence: run in progress

static void runBusyCase(const char* command) {
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    sup->takeMeasurementRestart();
    sendTick(10500);
    TEST_ASSERT_EQUAL_UINT16(LOW, sup->dutyCounts());
    Lines out = send(command, 10500);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(has(out, kRunBusy));
    TEST_ASSERT_FALSE(has(out, "PRBS LOAD READY"));
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    sendTick(10999);
    TEST_ASSERT_EQUAL_UINT16(LOW, sup->dutyCounts());
    // run continues and the stored sequence is untouched: after it ends a new run works
    sendTick(11000);
    sendPeriod(11000, R(26.0f), R(25.0f));
    sendTick(12000);
    out = sendPeriod(12000, R(26.0f), R(25.0f));
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END ok n=2"));
    out = send("prbs run\n", 13000);
    TEST_ASSERT_TRUE(hasExact(out, startLine(2)));
    TEST_ASSERT_TRUE(sup->takeMeasurementRestart());
}

void test_E1_prbs_load_refused_during_run() { runBusyCase("prbs load 3\n"); }
void test_E1_prbs_run_refused_during_run() { runBusyCase("prbs run\n"); }
void test_E1_start_refused_during_run() { runBusyCase("start\n"); }

void test_E1_start_refused_during_run_does_not_start_sweep() {
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    send("start 10\n", 10500);
    sendTick(11000);
    sendPeriod(11000, R(30.0f), R(25.0f));
    sendTick(12000);
    sendPeriod(12000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    Lines out = send("stop\n", 12100);
    TEST_ASSERT_TRUE(has(out, "NOTHING RUNNING"));
}

void test_E1_run_busy_wins_over_missing_sequence_checks() {
    // a load refused while running does not clear the stored sequence nor enter load mode
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    send("prbs load 2\n", 10100);
    Lines out = send("5\n", 10200);  // would be a load value if load mode had started
    TEST_ASSERT_FALSE(has(out, "PRBS LOAD"));
    TEST_ASSERT_FALSE(has(out, "REFUSED: value"));
}

// ---------------------------------------------------------------- precedence: sweep in progress

static void startSweepWithSequence() {
    sup->begin(0);
    measure(1000);
    loadSeq({LOW, HIGH}, 2000);
    Lines out = send("start\n", 3000);
    TEST_ASSERT_TRUE(has(out, "START from 0.0 %"));
}

static void afterSweepStopRunWorks() {
    Lines out = send("stop\n", 4000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("STOPPED", out[0].c_str());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    out = send("prbs run\n", 5000);
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    TEST_ASSERT_EQUAL_STRING(startLine(2).c_str(), out[0].c_str());
    TEST_ASSERT_TRUE(sup->takeMeasurementRestart());
}

void test_E2_prbs_load_refused_during_sweep() {
    startSweepWithSequence();
    sink.lines.clear();
    Lines out = send("prbs load 3\n", 3500);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kSweepBusy, out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    afterSweepStopRunWorks();
}

void test_E2_prbs_run_refused_during_sweep() {
    startSweepWithSequence();
    sink.lines.clear();
    Lines out = send("prbs run\n", 3500);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kSweepBusy, out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    afterSweepStopRunWorks();
}

void test_E2_sweep_busy_wins_over_no_sequence() {
    sup->begin(0);
    measure(1000);
    send("start\n", 1500);
    sink.lines.clear();
    Lines out = send("prbs run\n", 2000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kSweepBusy, out[0].c_str());
}

void test_E2_start_during_sweep_keeps_sweep_text() {
    startSweepWithSequence();
    sink.lines.clear();
    Lines out = send("start\n", 3500);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("REFUSED: a sweep is already running", out[0].c_str());
    afterSweepStopRunWorks();
}

// ---------------------------------------------------------------- latch by sweep cutoff

static void sweepCutoff() {
    sup->begin(0);
    measure(1000);
    sendPeriod(2000, P(80.0f, 1800.0f), R(25.0f));
}

void test_E3_E10_latch_by_sweep_cutoff_refuses_prbs_load() {
    sweepCutoff();
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    sink.lines.clear();
    Lines out = send("prbs load 3\n", 3000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
}

void test_E3_E10_latch_by_sweep_cutoff_refuses_prbs_run_even_without_sequence() {
    sweepCutoff();
    sink.lines.clear();
    Lines out = send("prbs run\n", 3000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_E3_E10_latch_by_sweep_cutoff_refuses_prbs_run_with_sequence() {
    sup->begin(0);
    measure(1000);
    loadSeq({LOW, HIGH}, 1500);
    sendPeriod(2000, P(80.0f, 1800.0f), R(25.0f));
    sink.lines.clear();
    sendPeriod(2500, R(25.0f), R(25.0f));  // temperature back to normal does not release the latch
    Lines out = send("prbs run\n", 3000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_E3_E10_latch_by_sweep_cutoff_refuses_start() {
    sweepCutoff();
    sink.lines.clear();
    Lines out = send("start\n", 3000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

// ---------------------------------------------------------------- stop during a run, abort and latch

void test_AC9_stop_during_run_zeroes_duty_and_prints_abort_line() {
    sup->begin(0);
    startRun({LOW, HIGH, LOW}, 10000);
    sendTick(11000);
    sendPeriod(11000, R(30.0f), R(25.0f));
    sendTick(11500);
    TEST_ASSERT_EQUAL_UINT16(HIGH, sup->dutyCounts());
    Lines out = send("stop\n", 11500);
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END aborted k=1 reason=stopped by user"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    sendTick(12500);
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC9_stop_at_once_before_any_line_gives_k0() {
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    Lines out = send("stop\n", 10200);
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END aborted k=0 reason=stopped by user"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC9_stop_does_not_latch_and_sweep_can_start_after() {
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    send("stop\n", 10200);
    sink.lines.clear();
    Lines out = send("start\n", 10300);
    TEST_ASSERT_TRUE(has(out, "START from 0.0 %"));
    out = send("stop\n", 10400);
    TEST_ASSERT_TRUE(hasExact(out, "STOPPED"));
}

void test_AC9_stop_during_run_then_new_run_is_possible() {
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    send("stop\n", 10200);
    sup->takeMeasurementRestart();
    Lines out = send("prbs run\n", 11000);
    TEST_ASSERT_TRUE(hasExact(out, startLine(2)));
    TEST_ASSERT_TRUE(sup->takeMeasurementRestart());
}

void test_AC8_over_temperature_aborts_zeroes_duty_and_no_sweep_cutoff() {
    sup->begin(0);
    startRun({LOW, HIGH, LOW}, 10000);
    sendTick(11000);
    Lines out = sendPeriod(11000, R(75.0f), R(25.0f));
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END aborted k=0 reason=over-temperature"));
    TEST_ASSERT_FALSE(has(out, "CUTOFF"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    sendTick(12000);
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC8_sensor_fault_aborts_via_prbs_not_sweep_cutoff() {
    sup->begin(0);
    startRun({LOW, HIGH, LOW}, 10000);
    sendTick(11000);
    Lines out = sendPeriod(11000, P(30.0f, 0.0f), R(25.0f));
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END aborted k=0 reason=sensor fault load"));
    TEST_ASSERT_FALSE(has(out, "CUTOFF"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC8_E3_after_prbs_abort_start_is_refused() {
    abortRun(10000);
    sink.lines.clear();
    Lines out = send("start\n", 12000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC8_E3_after_prbs_abort_prbs_run_is_refused() {
    abortRun(10000);
    sup->takeMeasurementRestart();
    sink.lines.clear();
    Lines out = send("prbs run\n", 12000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    TEST_ASSERT_FALSE(sup->takeMeasurementRestart());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC8_E3_after_prbs_abort_prbs_load_is_refused() {
    abortRun(10000);
    sink.lines.clear();
    Lines out = send("prbs load 3\n", 12000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    out = send("5\n", 12100);  // not a load stream
    TEST_ASSERT_FALSE(has(out, "REFUSED: value"));
}

void test_AC8_latch_survives_good_periods_and_ticks_until_new_supervisor() {
    abortRun(10000);
    for (uint32_t i = 2; i < 8; i++) {
        sendTick(10000 + i * 1000);
        sendPeriod(10000 + i * 1000, R(25.0f), R(25.0f));
    }
    sink.lines.clear();
    Lines out = send("start\n", 20000);
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    // a new Supervisor (reset) is free again
    delete sup;
    sup = new Supervisor(sink);
    sup->begin(0);
    measure(1000);
    sink.lines.clear();
    out = send("start\n", 2000);
    TEST_ASSERT_TRUE(has(out, "START from 0.0 %"));
}

// ---------------------------------------------------------------- replies during a run

void test_temp_during_run_is_answered_from_observed_values() {
    sup->begin(0);
    startRun({LOW, HIGH, LOW}, 10000);
    sendTick(11000);
    sendPeriod(11000, R(30.0f), R(26.0f));
    Lines out = send("temp\n", 11100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(has(out, "TEMP load="));
    TEST_ASSERT_TRUE(has(out, "30.0"));
    TEST_ASSERT_TRUE(has(out, "26.0"));
}

void test_prbs_foo_is_answered_like_unknown_command() {
    sup->begin(0);
    sink.lines.clear();
    Lines out = send("prbs foo\n", 1000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(
        "UNKNOWN COMMAND: valid commands: start [N], stop, results, temp, prbs load N, prbs run", out[0].c_str());
}

void test_prbs_foo_during_run_is_answered_with_the_unknown_text() {
    sup->begin(0);
    startRun({LOW, HIGH}, 10000);
    Lines out = send("prbs foo\n", 10100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(
        has(out, "UNKNOWN COMMAND: valid commands: start [N], stop, results, temp, prbs load N, prbs run"));
}

void test_no_sweep_log_line_appears_in_a_run() {
    sup->begin(0);
    std::vector<unsigned> v(12, LOW);
    startRun(v, 10000);
    Lines all;
    for (uint32_t k = 1; k <= 12; k++) {
        sendTick(10000 + k * 1000);
        Lines out = sendPeriod(10000 + k * 1000, R(30.0f), R(25.0f));
        all.insert(all.end(), out.begin(), out.end());
    }
    TEST_ASSERT_FALSE(has(all, "LOG"));
    TEST_ASSERT_FALSE(has(all, "CUTOFF"));
    TEST_ASSERT_EQUAL_UINT32(13, all.size());  // 12 data lines + RUN END
    TEST_ASSERT_TRUE(hasExact(all, "# RUN END ok n=12"));
}

// ---------------------------------------------------------------- sweep outside a run

void test_sweep_start_stop_outside_run() {
    sup->begin(0);
    sink.lines.clear();
    Lines out = send("start\n", 500);
    TEST_ASSERT_TRUE(has(out, "REFUSED"));  // no measurement yet
    measure(1000);
    out = send("start\n", 1100);
    TEST_ASSERT_TRUE(has(out, "START from 0.0 %"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
    out = send("stop\n", 1200);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("STOPPED", out[0].c_str());
    out = send("stop\n", 1300);
    TEST_ASSERT_TRUE(has(out, "NOTHING RUNNING"));
}

void test_sweep_reaches_a_nonzero_duty_under_supervisor() {
    sup->begin(0);
    measure(1000);
    send("start\n", 1100);
    Lines all;
    for (uint32_t i = 2; i <= 127; i++) {
        sendTick(i * 1000);
        Lines out = sendPeriod(i * 1000, R(25.0f), R(25.0f));
        all.insert(all.end(), out.begin(), out.end());
    }
    TEST_ASSERT_TRUE(sup->dutyCounts() > 0);
    TEST_ASSERT_TRUE(sup->dutyCounts() <= kCapCounts);
    // a PRBS command is refused while the sweep is in progress, and the sweep keeps its duty
    uint16_t d = sup->dutyCounts();
    Lines out = send("prbs run\n", 128000);
    TEST_ASSERT_EQUAL_STRING(kSweepBusy, out[0].c_str());
    TEST_ASSERT_EQUAL_UINT16(d, sup->dutyCounts());
    out = send("stop\n", 128100);
    TEST_ASSERT_TRUE(hasExact(out, "STOPPED"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

void test_AC4_duty_goes_back_to_sweep_duty_after_run_ends() {
    sup->begin(0);
    startRun({HIGH}, 10000);
    sendTick(10000);
    TEST_ASSERT_EQUAL_UINT16(HIGH, sup->dutyCounts());
    sendTick(11000);
    Lines out = sendPeriod(11000, R(30.0f), R(25.0f));
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END ok n=1"));
    TEST_ASSERT_EQUAL_UINT16(0, sup->dutyCounts());
}

// ---------------------------------------------------------------- T13: replies are '#' lines during a run

namespace {

const char* kUnknownText = "UNKNOWN COMMAND: valid commands: start [N], stop, results, temp, prbs load N, prbs run";

bool isNum(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool isTemp(const std::string& s) {
    if (s == "nan") return true;
    size_t dot = s.find('.');
    if (dot == std::string::npos) return false;
    return isNum(s.substr(0, dot)) && isNum(s.substr(dot + 1));
}

// <digits>,<digits>,<digits>,<temp>,<temp>
bool isDataLine(const std::string& s) {
    std::vector<std::string> f;
    size_t from = 0;
    for (;;) {
        size_t c = s.find(',', from);
        if (c == std::string::npos) {
            f.push_back(s.substr(from));
            break;
        }
        f.push_back(s.substr(from, c - from));
        from = c + 1;
    }
    return f.size() == 5 && isNum(f[0]) && isNum(f[1]) && isNum(f[2]) && isTemp(f[3]) && isTemp(f[4]);
}

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

// Starts a run of {LOW,HIGH} at 10000 and passes one period so that temp has values.
void runningSupervisor() {
    sup->begin(0);
    startRun({LOW, HIGH, LOW}, 10000);
    sendTick(11000);
    sendPeriod(11000, R(30.0f), R(26.0f));
}

}  // namespace

void test_AC6_whole_run_every_line_is_data_or_hash() {
    const unsigned N = 6;
    sup->begin(0);
    measure(1000);
    loadSeq({LOW, HIGH, HIGH, LOW, HIGH, LOW}, 2000);
    size_t from = sink.lines.size();
    send("prbs run\n", 10000);
    const char* cmds[] = {"temp\n", "results\n", "start\n", "prbs load 3\n", "prbs run\n", "prbs foo\n", "xyzzy\n"};
    for (uint32_t k = 1; k <= N; k++) {
        sendTick(10000 + k * 1000 - 500);
        for (const char* c : cmds) {
            Lines out = send(c, 10000 + k * 1000 - 400);
            TEST_ASSERT_TRUE_MESSAGE(out.size() >= 1, c);
            for (const auto& l : out) TEST_ASSERT_TRUE_MESSAGE(startsWith(l, "# "), l.c_str());
        }
        sendTick(10000 + k * 1000);
        sendPeriod(10000 + k * 1000, R(30.0f), R(26.0f));
    }
    Lines run(sink.lines.begin() + from, sink.lines.end());
    TEST_ASSERT_TRUE(run.size() > 2);
    TEST_ASSERT_EQUAL_STRING(startLine(N).c_str(), run.front().c_str());
    TEST_ASSERT_EQUAL_STRING("# RUN END ok n=6", run.back().c_str());
    unsigned data = 0;
    for (const auto& l : run) {
        if (isDataLine(l)) {
            data++;
            continue;
        }
        TEST_ASSERT_TRUE_MESSAGE(startsWith(l, "# "), l.c_str());
        TEST_ASSERT_FALSE_MESSAGE(startsWith(l, "# # "), l.c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(N, data);
    TEST_ASSERT_FALSE(has(run, "LOG"));
    TEST_ASSERT_FALSE(has(run, "CUTOFF"));
    TEST_ASSERT_TRUE(hasExact(run, kColumns));
    TEST_ASSERT_TRUE(hasExact(run, std::string("# ") + kRunBusy));
    TEST_ASSERT_TRUE(hasExact(run, std::string("# ") + kUnknownText));
    TEST_ASSERT_TRUE(has(run, "# TEMP load="));
}

void test_AC6_E2_temp_during_run_is_hash_temp() {
    runningSupervisor();
    Lines out = send("temp\n", 11100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(startsWith(out[0], "# TEMP load="));
    TEST_ASSERT_TRUE(has(out, "30.0"));
    TEST_ASSERT_TRUE(has(out, "26.0"));
}

void test_AC6_refused_start_during_run_is_hash_refused() {
    runningSupervisor();
    Lines out = send("start\n", 11100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("# REFUSED: a PRBS run is in progress", out[0].c_str());
}

void test_AC6_refused_prbs_load_during_run_is_hash_refused() {
    runningSupervisor();
    Lines out = send("prbs load 3\n", 11100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("# REFUSED: a PRBS run is in progress", out[0].c_str());
}

void test_AC6_refused_prbs_run_during_run_is_hash_refused() {
    runningSupervisor();
    Lines out = send("prbs run\n", 11100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("# REFUSED: a PRBS run is in progress", out[0].c_str());
}

void test_AC6_results_during_run_is_a_hash_line() {
    runningSupervisor();
    Lines out = send("results\n", 11100);
    TEST_ASSERT_TRUE(out.size() >= 1);
    for (const auto& l : out) TEST_ASSERT_TRUE_MESSAGE(startsWith(l, "# "), l.c_str());
}

void test_AC6_unknown_and_malformed_prbs_during_run_are_hash_lines() {
    runningSupervisor();
    Lines out = send("xyzzy\n", 11100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING((std::string("# ") + kUnknownText).c_str(), out[0].c_str());
    out = send("prbs foo\n", 11200);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING((std::string("# ") + kUnknownText).c_str(), out[0].c_str());
}

void test_AC6_run_lines_are_not_double_prefixed_and_data_has_no_prefix() {
    sup->begin(0);
    measure(1000);
    loadSeq({LOW, HIGH}, 2000);
    Lines out = send("prbs run\n", 10000);
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    TEST_ASSERT_EQUAL_STRING(startLine(2).c_str(), out[0].c_str());
    TEST_ASSERT_EQUAL_STRING(kColumns, out[1].c_str());
    sendTick(11000);
    out = sendPeriod(11000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(isDataLine(out[0]));
    sendTick(12000);
    out = sendPeriod(12000, R(30.0f), R(25.0f));
    TEST_ASSERT_EQUAL_UINT32(2, out.size());
    TEST_ASSERT_TRUE(isDataLine(out[0]));
    TEST_ASSERT_EQUAL_STRING("# RUN END ok n=2", out[1].c_str());
}

void test_AC6_replies_are_unprefixed_before_the_run() {
    sup->begin(0);
    measure(1000);
    loadSeq({LOW, HIGH}, 2000);
    Lines out = send("temp\n", 3000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(startsWith(out[0], "TEMP load="));
    out = send("xyzzy\n", 3100);
    TEST_ASSERT_EQUAL_STRING(kUnknownText, out[0].c_str());
}

void test_AC6_replies_are_unprefixed_after_run_end_ok() {
    sup->begin(0);
    startRun({LOW}, 10000);
    sendTick(11000);
    Lines out = sendPeriod(11000, R(30.0f), R(25.0f));
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END ok n=1"));
    out = send("temp\n", 11100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(startsWith(out[0], "TEMP load="));
    out = send("xyzzy\n", 11200);
    TEST_ASSERT_EQUAL_STRING(kUnknownText, out[0].c_str());
    out = send("stop\n", 11300);
    TEST_ASSERT_TRUE(has(out, "NOTHING RUNNING"));
    TEST_ASSERT_FALSE(startsWith(out[0], "#"));
}

void test_AC6_replies_are_unprefixed_after_stop_mid_run() {
    runningSupervisor();
    Lines out = send("stop\n", 11500);
    TEST_ASSERT_TRUE(hasExact(out, "# RUN END aborted k=1 reason=stopped by user"));
    for (const auto& l : out) TEST_ASSERT_FALSE_MESSAGE(startsWith(l, "# # "), l.c_str());
    out = send("temp\n", 11600);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(startsWith(out[0], "TEMP load="));
    out = send("xyzzy\n", 11700);
    TEST_ASSERT_EQUAL_STRING(kUnknownText, out[0].c_str());
}

void test_AC6_replies_are_unprefixed_after_safety_abort() {
    abortRun(10000);
    Lines out = send("temp\n", 12000);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_TRUE(startsWith(out[0], "TEMP load="));
    out = send("start\n", 12100);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING(kLatch, out[0].c_str());
    out = send("xyzzy\n", 12200);
    TEST_ASSERT_EQUAL_STRING(kUnknownText, out[0].c_str());
}

void test_AC6_no_log_line_in_a_run_with_commands_between_periods() {
    sup->begin(0);
    std::vector<unsigned> v(8, HIGH);
    measure(1000);
    loadSeq(v, 2000);
    size_t from = sink.lines.size();
    send("prbs run\n", 10000);
    for (uint32_t k = 1; k <= 8; k++) {
        sendTick(10000 + k * 1000);
        sendPeriod(10000 + k * 1000, R(30.0f), R(25.0f));
        send("temp\n", 10000 + k * 1000 + 10);
    }
    Lines run(sink.lines.begin() + from, sink.lines.end());
    TEST_ASSERT_FALSE(has(run, "LOG"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_prints_ready_and_commands_lines);
    RUN_TEST(test_AC1_full_load_through_onChar_and_tick);
    RUN_TEST(test_load_characters_do_not_reach_the_command_parser);
    RUN_TEST(test_E11_stop_inside_load_stream_refuses_load_and_returns_to_command_mode);
    RUN_TEST(test_E7_load_timeout_through_tick);
    RUN_TEST(test_E12_bad_length_through_command);
    RUN_TEST(test_AC3_run_refused_without_sequence);
    RUN_TEST(test_AC3_run_refused_before_any_period);
    RUN_TEST(test_AC3_zero_sample_period_is_not_a_measurement);
    RUN_TEST(test_AC3_run_refused_load_not_at_ambient_then_accepted_later);
    RUN_TEST(test_AC3_run_accepted_prints_start_and_columns);
    RUN_TEST(test_D5_measurement_restart_flag_is_one_shot);
    RUN_TEST(test_AC4_duty_is_the_runs_duty_while_running);
    RUN_TEST(test_AC5_run_data_lines_and_end_through_supervisor);
    RUN_TEST(test_E1_prbs_load_refused_during_run);
    RUN_TEST(test_E1_prbs_run_refused_during_run);
    RUN_TEST(test_E1_start_refused_during_run);
    RUN_TEST(test_E1_start_refused_during_run_does_not_start_sweep);
    RUN_TEST(test_E1_run_busy_wins_over_missing_sequence_checks);
    RUN_TEST(test_E2_prbs_load_refused_during_sweep);
    RUN_TEST(test_E2_prbs_run_refused_during_sweep);
    RUN_TEST(test_E2_sweep_busy_wins_over_no_sequence);
    RUN_TEST(test_E2_start_during_sweep_keeps_sweep_text);
    RUN_TEST(test_E3_E10_latch_by_sweep_cutoff_refuses_prbs_load);
    RUN_TEST(test_E3_E10_latch_by_sweep_cutoff_refuses_prbs_run_even_without_sequence);
    RUN_TEST(test_E3_E10_latch_by_sweep_cutoff_refuses_prbs_run_with_sequence);
    RUN_TEST(test_E3_E10_latch_by_sweep_cutoff_refuses_start);
    RUN_TEST(test_AC9_stop_during_run_zeroes_duty_and_prints_abort_line);
    RUN_TEST(test_AC9_stop_at_once_before_any_line_gives_k0);
    RUN_TEST(test_AC9_stop_does_not_latch_and_sweep_can_start_after);
    RUN_TEST(test_AC9_stop_during_run_then_new_run_is_possible);
    RUN_TEST(test_AC8_over_temperature_aborts_zeroes_duty_and_no_sweep_cutoff);
    RUN_TEST(test_AC8_sensor_fault_aborts_via_prbs_not_sweep_cutoff);
    RUN_TEST(test_AC8_E3_after_prbs_abort_start_is_refused);
    RUN_TEST(test_AC8_E3_after_prbs_abort_prbs_run_is_refused);
    RUN_TEST(test_AC8_E3_after_prbs_abort_prbs_load_is_refused);
    RUN_TEST(test_AC8_latch_survives_good_periods_and_ticks_until_new_supervisor);
    RUN_TEST(test_temp_during_run_is_answered_from_observed_values);
    RUN_TEST(test_prbs_foo_is_answered_like_unknown_command);
    RUN_TEST(test_prbs_foo_during_run_is_answered_with_the_unknown_text);
    RUN_TEST(test_no_sweep_log_line_appears_in_a_run);
    RUN_TEST(test_sweep_start_stop_outside_run);
    RUN_TEST(test_sweep_reaches_a_nonzero_duty_under_supervisor);
    RUN_TEST(test_AC4_duty_goes_back_to_sweep_duty_after_run_ends);
    RUN_TEST(test_AC6_whole_run_every_line_is_data_or_hash);
    RUN_TEST(test_AC6_E2_temp_during_run_is_hash_temp);
    RUN_TEST(test_AC6_refused_start_during_run_is_hash_refused);
    RUN_TEST(test_AC6_refused_prbs_load_during_run_is_hash_refused);
    RUN_TEST(test_AC6_refused_prbs_run_during_run_is_hash_refused);
    RUN_TEST(test_AC6_results_during_run_is_a_hash_line);
    RUN_TEST(test_AC6_unknown_and_malformed_prbs_during_run_are_hash_lines);
    RUN_TEST(test_AC6_run_lines_are_not_double_prefixed_and_data_has_no_prefix);
    RUN_TEST(test_AC6_replies_are_unprefixed_before_the_run);
    RUN_TEST(test_AC6_replies_are_unprefixed_after_run_end_ok);
    RUN_TEST(test_AC6_replies_are_unprefixed_after_stop_mid_run);
    RUN_TEST(test_AC6_replies_are_unprefixed_after_safety_abort);
    RUN_TEST(test_AC6_no_log_line_in_a_run_with_commands_between_periods);
    return UNITY_END();
}
