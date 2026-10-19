#include <unity.h>

#include <stdint.h>

#include "SweepConfig.h"  // D12 tunables: must exist and compile on the host
#include "duty.h"

void setUp() {}
void tearDown() {}

void test_AC10_constants_full_scale_and_cap() {
    TEST_ASSERT_EQUAL_UINT16(4095, kFullScale);
    TEST_ASSERT_EQUAL_UINT16(1023, kCapCounts);
}

void test_AC10_clampDuty_limits_1024_to_cap() {
    TEST_ASSERT_EQUAL_UINT16(1023, clampDuty(1024));
}

void test_AC10_clampDuty_limits_full_scale_to_cap() {
    TEST_ASSERT_EQUAL_UINT16(1023, clampDuty(4095));
}

void test_AC10_clampDuty_limits_65535_to_cap() {
    TEST_ASSERT_EQUAL_UINT16(1023, clampDuty(65535));
}

void test_AC10_clampDuty_passes_values_at_or_below_cap() {
    TEST_ASSERT_EQUAL_UINT16(0, clampDuty(0));
    TEST_ASSERT_EQUAL_UINT16(1, clampDuty(1));
    TEST_ASSERT_EQUAL_UINT16(512, clampDuty(512));
    TEST_ASSERT_EQUAL_UINT16(1022, clampDuty(1022));
    TEST_ASSERT_EQUAL_UINT16(1023, clampDuty(1023));
}

void test_E8_percentToCounts_25_percent_is_1023() {
    TEST_ASSERT_EQUAL_UINT16(1023, percentToCounts(25.0f));
}

void test_E8_percentToCounts_2_5_percent_is_102() {
    TEST_ASSERT_EQUAL_UINT16(102, percentToCounts(2.5f));
}

void test_E8_percentToCounts_zero_is_zero() {
    TEST_ASSERT_EQUAL_UINT16(0, percentToCounts(0.0f));
}

void test_E8_percentToCounts_rounds_to_nearest() {
    // 5.0 % -> 204.75 -> 205 ; 7.5 % -> 307.125 -> 307
    // 12.5 % -> 511.875 -> 512 ; 22.5 % -> 921.375 -> 921
    TEST_ASSERT_EQUAL_UINT16(205, percentToCounts(5.0f));
    TEST_ASSERT_EQUAL_UINT16(307, percentToCounts(7.5f));
    TEST_ASSERT_EQUAL_UINT16(512, percentToCounts(12.5f));
    TEST_ASSERT_EQUAL_UINT16(921, percentToCounts(22.5f));
}

void test_AC10_percentToCounts_never_exceeds_cap() {
    TEST_ASSERT_EQUAL_UINT16(1023, percentToCounts(25.1f));
    TEST_ASSERT_EQUAL_UINT16(1023, percentToCounts(50.0f));
    TEST_ASSERT_EQUAL_UINT16(1023, percentToCounts(100.0f));
    for (int i = 0; i <= 400; ++i) {
        float pct = 0.25f * (float)i;  // 0 .. 100 %
        TEST_ASSERT_TRUE(percentToCounts(pct) <= 1023);
    }
}

void test_E8_countsToPercent_uses_4095_scale() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, countsToPercent(0));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.98f, countsToPercent(1023));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, countsToPercent(4095));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.49f, countsToPercent(102));
}


// ---- single place for the maximum duty: everything derives from sweepcfg::kCapPercent ----

void test_AC10_cfg_kCapCounts_is_floor_of_cap_percent_of_full_scale() {
    uint16_t expected = (uint16_t)(sweepcfg::kCapPercent * kFullScale / 100.0f);
    TEST_ASSERT_EQUAL_UINT16(expected, kCapCounts);
}

void test_AC10_cfg_clampDuty_limits_to_derived_cap() {
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, clampDuty((uint16_t)(kCapCounts + 1)));
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, clampDuty(4095));
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, clampDuty(65535));
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, clampDuty(kCapCounts));
}

void test_AC10_cfg_percentToCounts_of_cap_percent_is_cap_counts() {
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, percentToCounts(sweepcfg::kCapPercent));
    TEST_ASSERT_EQUAL_UINT16(kCapCounts, percentToCounts(100.0f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_AC10_constants_full_scale_and_cap);
    RUN_TEST(test_AC10_clampDuty_limits_1024_to_cap);
    RUN_TEST(test_AC10_clampDuty_limits_full_scale_to_cap);
    RUN_TEST(test_AC10_clampDuty_limits_65535_to_cap);
    RUN_TEST(test_AC10_clampDuty_passes_values_at_or_below_cap);
    RUN_TEST(test_E8_percentToCounts_25_percent_is_1023);
    RUN_TEST(test_E8_percentToCounts_2_5_percent_is_102);
    RUN_TEST(test_E8_percentToCounts_zero_is_zero);
    RUN_TEST(test_E8_percentToCounts_rounds_to_nearest);
    RUN_TEST(test_AC10_percentToCounts_never_exceeds_cap);
    RUN_TEST(test_E8_countsToPercent_uses_4095_scale);
    RUN_TEST(test_AC10_cfg_kCapCounts_is_floor_of_cap_percent_of_full_scale);
    RUN_TEST(test_AC10_cfg_clampDuty_limits_to_derived_cap);
    RUN_TEST(test_AC10_cfg_percentToCounts_of_cap_percent_is_cap_counts);
    return UNITY_END();
}
