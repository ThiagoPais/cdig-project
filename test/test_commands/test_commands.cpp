#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "SweepConfig.h"

#include "commands.h"

void setUp() {}
void tearDown() {}

namespace {

// Pushes every character of `text` into `lb`; collects each completed command in order.
struct Collected {
    Command cmds[8];
    int count;
};

Collected feed(LineBuffer& lb, const char* text) {
    Collected r;
    r.count = 0;
    for (size_t i = 0; i < strlen(text); ++i) {
        Command c;
        c.type = CmdType::None;
        c.startPercent = -999.0f;
        if (lb.push(text[i], &c)) {
            if (r.count < 8) r.cmds[r.count] = c;
            r.count++;
        }
    }
    return r;
}

// Feeds one line (with its terminator already in `text`) and expects exactly one command.
Command parseOne(const char* text) {
    LineBuffer lb;
    Collected r = feed(lb, text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.count, text);
    return r.cmds[0];
}

}  // namespace

// ---- AC2: command forms ----

void test_AC2_start_without_number_is_start_zero() {
    Command c = parseOne("start\n");
    TEST_ASSERT_TRUE(c.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.startPercent);
}

void test_AC2_start_10() {
    Command c = parseOne("start 10\n");
    TEST_ASSERT_TRUE(c.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, c.startPercent);
}

void test_AC2_start_0_and_25_are_accepted_bounds() {
    Command a = parseOne("start 0\n");
    TEST_ASSERT_TRUE(a.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.startPercent);
    Command b = parseOne("start 25\n");
    TEST_ASSERT_TRUE(b.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, b.startPercent);
}

void test_AC2_start_non_multiple_of_2_5_is_accepted() {
    Command c = parseOne("start 12.3\n");
    TEST_ASSERT_TRUE(c.type == CmdType::Start);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.3f, c.startPercent);
}

void test_AC2_stop() {
    TEST_ASSERT_TRUE(parseOne("stop\n").type == CmdType::Stop);
}

void test_AC2_results() {
    TEST_ASSERT_TRUE(parseOne("results\n").type == CmdType::Results);
}

void test_AC2_temp() {
    TEST_ASSERT_TRUE(parseOne("temp\n").type == CmdType::Temp);
}

void test_AC2_no_command_before_newline() {
    LineBuffer lb;
    Collected r = feed(lb, "stop");
    TEST_ASSERT_EQUAL_INT(0, r.count);
}

// ---- E4: trimming, case sensitivity, empty lines, unknown ----

void test_E4_cr_lf_terminator_is_trimmed() {
    Command c = parseOne("stop\r\n");
    TEST_ASSERT_TRUE(c.type == CmdType::Stop);
}

void test_E4_leading_and_trailing_spaces_trimmed() {
    Command c = parseOne("  results  \r\n");
    TEST_ASSERT_TRUE(c.type == CmdType::Results);
    Command d = parseOne("  start 10 \r\n");
    TEST_ASSERT_TRUE(d.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, d.startPercent);
}

void test_E4_trailing_space_after_bare_start() {
    Command c = parseOne("start \n");
    TEST_ASSERT_TRUE(c.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.startPercent);
}

void test_E4_case_sensitive() {
    TEST_ASSERT_TRUE(parseOne("Start\n").type == CmdType::Unknown);
    TEST_ASSERT_TRUE(parseOne("STOP\n").type == CmdType::Unknown);
    TEST_ASSERT_TRUE(parseOne("Temp\n").type == CmdType::Unknown);
}

void test_E4_unknown_word() {
    TEST_ASSERT_TRUE(parseOne("foo\n").type == CmdType::Unknown);
    TEST_ASSERT_TRUE(parseOne("startx\n").type == CmdType::Unknown);
    TEST_ASSERT_TRUE(parseOne("stopp\n").type == CmdType::Unknown);
}

void test_E4_empty_lines_are_ignored() {
    LineBuffer lb;
    Collected r = feed(lb, "\n");
    TEST_ASSERT_EQUAL_INT(0, r.count);
    r = feed(lb, "\r\n");
    TEST_ASSERT_EQUAL_INT(0, r.count);
    r = feed(lb, "   \r\n");
    TEST_ASSERT_EQUAL_INT(0, r.count);
}

void test_E4_empty_lines_do_not_disturb_next_command() {
    LineBuffer lb;
    Collected r = feed(lb, "\n\r\n  \nstop\n");
    TEST_ASSERT_EQUAL_INT(1, r.count);
    TEST_ASSERT_TRUE(r.cmds[0].type == CmdType::Stop);
}

// ---- E13: invalid start N ----

void test_E13_start_negative_is_bad_start() {
    TEST_ASSERT_TRUE(parseOne("start -1\n").type == CmdType::BadStart);
}

void test_E13_start_above_25_is_bad_start() {
    TEST_ASSERT_TRUE(parseOne("start 26\n").type == CmdType::BadStart);
    TEST_ASSERT_TRUE(parseOne("start 25.1\n").type == CmdType::BadStart);
}

void test_E13_start_non_numeric_is_bad_start() {
    TEST_ASSERT_TRUE(parseOne("start abc\n").type == CmdType::BadStart);
}

// ---- E12: back to back lines handled in arrival order ----

void test_E12_two_lines_back_to_back_in_order() {
    LineBuffer lb;
    Collected r = feed(lb, "start 5\nstop\n");
    TEST_ASSERT_EQUAL_INT(2, r.count);
    TEST_ASSERT_TRUE(r.cmds[0].type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, r.cmds[0].startPercent);
    TEST_ASSERT_TRUE(r.cmds[1].type == CmdType::Stop);
}

void test_E12_back_to_back_with_cr_and_unknown_in_between() {
    LineBuffer lb;
    Collected r = feed(lb, "temp\r\nbogus\r\nresults\r\n");
    TEST_ASSERT_EQUAL_INT(3, r.count);
    TEST_ASSERT_TRUE(r.cmds[0].type == CmdType::Temp);
    TEST_ASSERT_TRUE(r.cmds[1].type == CmdType::Unknown);
    TEST_ASSERT_TRUE(r.cmds[2].type == CmdType::Results);
}

void test_E12_line_split_across_calls() {
    LineBuffer lb;
    Collected a = feed(lb, "sta");
    TEST_ASSERT_EQUAL_INT(0, a.count);
    Collected b = feed(lb, "rt 7.5\n");
    TEST_ASSERT_EQUAL_INT(1, b.count);
    TEST_ASSERT_TRUE(b.cmds[0].type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(7.5f, b.cmds[0].startPercent);
}


// ---- E13 / AC2: the largest accepted start value is sweepcfg::kCapPercent ----

void test_E13_cfg_start_at_cap_percent_is_accepted() {
    char buf[48];
    snprintf(buf, sizeof buf, "start %g\n", (double)sweepcfg::kCapPercent);
    Command c = parseOne(buf);
    TEST_ASSERT_TRUE(c.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(sweepcfg::kCapPercent, c.startPercent);
}

void test_E13_cfg_start_above_cap_percent_is_bad_start() {
    char buf[48];
    snprintf(buf, sizeof buf, "start %g\n", (double)(sweepcfg::kCapPercent + 0.5f));
    TEST_ASSERT_TRUE(parseOne(buf).type == CmdType::BadStart);
}

// ---- T6 (AC1, AC3 command forms): prbs commands ----

namespace {
// Like parseOne, but pre-fills count with a sentinel so a parser that forgets to set it is caught.
Command parseOneSentinel(const char* text) {
    LineBuffer lb;
    Command out;
    out.type = CmdType::None;
    out.startPercent = -999.0f;
    out.count = 12345u;
    int done = 0;
    Command result = out;
    for (size_t i = 0; i < strlen(text); ++i) {
        Command c = out;
        if (lb.push(text[i], &c)) {
            result = c;
            done++;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, done, text);
    return result;
}
}  // namespace

void test_AC1_prbs_load_31500() {
    Command c = parseOneSentinel("prbs load 31500\n");
    TEST_ASSERT_TRUE(c.type == CmdType::PrbsLoad);
    TEST_ASSERT_EQUAL_UINT32(31500u, c.count);
}

void test_AC1_prbs_load_zero() {
    Command c = parseOneSentinel("prbs load 0\n");
    TEST_ASSERT_TRUE(c.type == CmdType::PrbsLoad);
    TEST_ASSERT_EQUAL_UINT32(0u, c.count);
}

void test_AC1_prbs_load_nine_digits_is_exact() {
    Command c = parseOneSentinel("prbs load 999999999\n");
    TEST_ASSERT_TRUE(c.type == CmdType::PrbsLoad);
    TEST_ASSERT_EQUAL_UINT32(999999999u, c.count);
}

void test_AC1_prbs_load_more_than_nine_digits_saturates() {
    Command c = parseOneSentinel("prbs load 1000000000\n");
    TEST_ASSERT_TRUE(c.type == CmdType::PrbsLoad);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, c.count);
    Command d = parseOneSentinel("prbs load 99999999999999999999\n");
    TEST_ASSERT_TRUE(d.type == CmdType::PrbsLoad);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, d.count);
}

void test_AC1_prbs_load_trimmed_with_crlf() {
    Command c = parseOneSentinel("  prbs load 42 \r\n");
    TEST_ASSERT_TRUE(c.type == CmdType::PrbsLoad);
    TEST_ASSERT_EQUAL_UINT32(42u, c.count);
}

void test_AC3_prbs_run() {
    Command c = parseOneSentinel("prbs run\n");
    TEST_ASSERT_TRUE(c.type == CmdType::PrbsRun);
    TEST_ASSERT_EQUAL_UINT32(0u, c.count);
}

void test_AC3_prbs_run_trimmed_with_crlf() {
    TEST_ASSERT_TRUE(parseOneSentinel("  prbs run  \r\n").type == CmdType::PrbsRun);
}

void test_AC1_bad_prbs_forms() {
    TEST_ASSERT_TRUE(parseOneSentinel("prbs\n").type == CmdType::BadPrbs);
    TEST_ASSERT_TRUE(parseOneSentinel("prbs load\n").type == CmdType::BadPrbs);
    TEST_ASSERT_TRUE(parseOneSentinel("prbs load x\n").type == CmdType::BadPrbs);
    TEST_ASSERT_TRUE(parseOneSentinel("prbs foo\n").type == CmdType::BadPrbs);
}

void test_AC1_prbs_is_case_sensitive() {
    TEST_ASSERT_TRUE(parseOneSentinel("PRBS run\n").type != CmdType::PrbsRun);
    TEST_ASSERT_TRUE(parseOneSentinel("prbs RUN\n").type == CmdType::BadPrbs);
}

void test_T6_count_is_zero_for_non_load_commands() {
    TEST_ASSERT_EQUAL_UINT32(0u, parseOneSentinel("start 5\n").count);
    TEST_ASSERT_EQUAL_UINT32(0u, parseOneSentinel("stop\n").count);
    TEST_ASSERT_EQUAL_UINT32(0u, parseOneSentinel("results\n").count);
    TEST_ASSERT_EQUAL_UINT32(0u, parseOneSentinel("temp\n").count);
    TEST_ASSERT_EQUAL_UINT32(0u, parseOneSentinel("bogus\n").count);
    TEST_ASSERT_EQUAL_UINT32(0u, parseOneSentinel("start abc\n").count);
    TEST_ASSERT_EQUAL_UINT32(0u, parseOneSentinel("prbs foo\n").count);
}

void test_T6_sweep_commands_unchanged_with_prbs_present() {
    TEST_ASSERT_TRUE(parseOneSentinel("start 10\n").type == CmdType::Start);
    TEST_ASSERT_TRUE(parseOneSentinel("stop\n").type == CmdType::Stop);
    TEST_ASSERT_TRUE(parseOneSentinel("foo\n").type == CmdType::Unknown);
    TEST_ASSERT_TRUE(parseOneSentinel("start abc\n").type == CmdType::BadStart);
}

void test_T6_command_aggregate_init_with_two_members_compiles() {
    float pct = 5.0f;
    Command c{CmdType::Start, pct};
    TEST_ASSERT_TRUE(c.type == CmdType::Start);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, c.startPercent);
    TEST_ASSERT_EQUAL_UINT32(0u, c.count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_AC1_prbs_load_31500);
    RUN_TEST(test_AC1_prbs_load_zero);
    RUN_TEST(test_AC1_prbs_load_nine_digits_is_exact);
    RUN_TEST(test_AC1_prbs_load_more_than_nine_digits_saturates);
    RUN_TEST(test_AC1_prbs_load_trimmed_with_crlf);
    RUN_TEST(test_AC3_prbs_run);
    RUN_TEST(test_AC3_prbs_run_trimmed_with_crlf);
    RUN_TEST(test_AC1_bad_prbs_forms);
    RUN_TEST(test_AC1_prbs_is_case_sensitive);
    RUN_TEST(test_T6_count_is_zero_for_non_load_commands);
    RUN_TEST(test_T6_sweep_commands_unchanged_with_prbs_present);
    RUN_TEST(test_T6_command_aggregate_init_with_two_members_compiles);
    RUN_TEST(test_AC2_start_without_number_is_start_zero);
    RUN_TEST(test_AC2_start_10);
    RUN_TEST(test_AC2_start_0_and_25_are_accepted_bounds);
    RUN_TEST(test_AC2_start_non_multiple_of_2_5_is_accepted);
    RUN_TEST(test_AC2_stop);
    RUN_TEST(test_AC2_results);
    RUN_TEST(test_AC2_temp);
    RUN_TEST(test_AC2_no_command_before_newline);
    RUN_TEST(test_E4_cr_lf_terminator_is_trimmed);
    RUN_TEST(test_E4_leading_and_trailing_spaces_trimmed);
    RUN_TEST(test_E4_trailing_space_after_bare_start);
    RUN_TEST(test_E4_case_sensitive);
    RUN_TEST(test_E4_unknown_word);
    RUN_TEST(test_E4_empty_lines_are_ignored);
    RUN_TEST(test_E4_empty_lines_do_not_disturb_next_command);
    RUN_TEST(test_E13_start_negative_is_bad_start);
    RUN_TEST(test_E13_start_above_25_is_bad_start);
    RUN_TEST(test_E13_start_non_numeric_is_bad_start);
    RUN_TEST(test_E12_two_lines_back_to_back_in_order);
    RUN_TEST(test_E12_back_to_back_with_cr_and_unknown_in_between);
    RUN_TEST(test_E12_line_split_across_calls);
    RUN_TEST(test_E13_cfg_start_at_cap_percent_is_accepted);
    RUN_TEST(test_E13_cfg_start_above_cap_percent_is_bad_start);
    return UNITY_END();
}
