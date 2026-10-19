#include <unity.h>

#include <math.h>
#include <stdint.h>

#include "measure.h"

void setUp() {}
void tearDown() {}

// Ideal 12-bit count for a voltage at 1.1 V full scale.
static float countForVolts(float volts) {
    return roundf(volts / 1.1f * 4095.0f);
}

static void expectTemp(float volts, float expectedC) {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expectedC, countToCelsius(countForVolts(volts)));
}

void test_AC12_0250V_is_25C() { expectTemp(0.250f, 25.0f); }
void test_AC12_0550V_is_55C() { expectTemp(0.550f, 55.0f); }
void test_AC12_0600V_is_60C() { expectTemp(0.600f, 60.0f); }
void test_AC12_0650V_is_65C() { expectTemp(0.650f, 65.0f); }
void test_AC12_0800V_is_80C() { expectTemp(0.800f, 80.0f); }

void test_AC14_window_mean_of_injected_samples() {
    WindowAverager a;
    a.reset();
    a.add(100);
    a.add(200);
    a.add(300);
    PeriodResult r = a.result();
    TEST_ASSERT_EQUAL_UINT16(3, r.samples);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, r.meanCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, countToCelsius(200.0f), r.tempC);
}

void test_AC14_mean_is_fractional_not_truncated() {
    WindowAverager a;
    a.reset();
    a.add(10);
    a.add(11);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.5f, a.result().meanCount);
}

void test_AC14_mean_of_many_full_scale_samples_does_not_overflow() {
    WindowAverager a;
    a.reset();
    for (int i = 0; i < 400; i++) a.add(4095);
    PeriodResult r = a.result();
    TEST_ASSERT_EQUAL_UINT16(400, r.samples);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4095.0f, r.meanCount);
}

void test_AC14_no_values_from_earlier_period_mixed_in() {
    WindowAverager a;
    a.reset();
    a.add(1000);
    a.add(1000);
    a.reset();
    a.add(2000);
    PeriodResult r = a.result();
    TEST_ASSERT_EQUAL_UINT16(1, r.samples);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, r.meanCount);
}

void test_AC14_empty_period_has_zero_samples_and_holds_previous_values() {
    WindowAverager a;
    a.reset();
    a.add(500);
    a.add(700);
    PeriodResult first = a.result();
    a.reset();  // new period, no samples added
    PeriodResult held = a.result();
    TEST_ASSERT_EQUAL_UINT16(0, held.samples);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, first.meanCount, held.meanCount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, first.tempC, held.tempC);
}

void test_AC14_takeSampleNow_only_in_first_800ms_at_2ms_spacing() {
    PeriodScheduler s;
    int count = 0;
    bool havePrev = false;
    uint32_t prev = 0;
    for (uint32_t t = 0; t < 1000; t++) {
        if (s.takeSampleNow(t)) {
            TEST_ASSERT_TRUE_MESSAGE(t < 800, "sample taken after 800 ms");
            if (havePrev) TEST_ASSERT_EQUAL_UINT32(2, t - prev);
            prev = t;
            havePrev = true;
            count++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(100, count);
    TEST_ASSERT_EQUAL_INT(400, count);
}

void test_AC14_no_sample_before_2ms_elapsed() {
    PeriodScheduler s;
    TEST_ASSERT_TRUE(s.takeSampleNow(0));
    TEST_ASSERT_FALSE(s.takeSampleNow(1));
    TEST_ASSERT_TRUE(s.takeSampleNow(2));
}

void test_AC14_exactly_one_period_end_per_1000ms() {
    PeriodScheduler s;
    int ends = 0;
    for (uint32_t t = 0; t < 3000; t++) {
        s.takeSampleNow(t);
        if (s.periodEnded(t)) {
            TEST_ASSERT_TRUE_MESSAGE(t == 1000 || t == 2000, "period ended at wrong time");
            ends++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, ends);
}

void test_AC14_sampling_window_repeats_in_second_period() {
    PeriodScheduler s;
    int inWindow = 0;
    for (uint32_t t = 0; t < 2000; t++) {
        bool take = s.takeSampleNow(t);
        s.periodEnded(t);
        if (t >= 1000 && take) {
            TEST_ASSERT_TRUE_MESSAGE(t < 1800, "sample after 800 ms of second period");
            inWindow++;
        }
        if (t >= 800 && t < 1000) TEST_ASSERT_FALSE(take);
    }
    TEST_ASSERT_EQUAL_INT(400, inWindow);
}

// ---- T7: PeriodScheduler::restart (plan D5) ----

// Runs a scheduler for a while so it has an old origin, then restarts at t0.
static void warmUp(PeriodScheduler& s, uint32_t untilMs) {
    for (uint32_t t = 0; t <= untilMs; t++) {
        s.takeSampleNow(t);
        s.periodEnded(t);
    }
}

void test_AC4_restart_period_ends_exactly_once_at_t_plus_1000() {
    PeriodScheduler s;
    warmUp(s, 4500);  // old origin 0 would end a period at 5000
    const uint32_t t0 = 4500;
    s.restart(t0);
    int ends = 0;
    for (uint32_t t = t0; t < t0 + 3000; t++) {
        s.takeSampleNow(t);
        if (s.periodEnded(t)) {
            TEST_ASSERT_TRUE_MESSAGE(t == t0 + 1000 || t == t0 + 2000,
                                     "period ended at wrong time after restart");
            ends++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, ends);
}

void test_AC4_restart_periodEnded_false_before_t_plus_1000_true_at_it() {
    PeriodScheduler s;
    warmUp(s, 2300);
    const uint32_t t0 = 2300;
    s.restart(t0);
    for (uint32_t t = t0; t < t0 + 1000; t++) TEST_ASSERT_FALSE(s.periodEnded(t));
    TEST_ASSERT_TRUE(s.periodEnded(t0 + 1000));
    TEST_ASSERT_FALSE(s.periodEnded(t0 + 1001));
}

void test_AC4_restart_sample_allowed_at_t_even_if_sampled_1ms_before() {
    PeriodScheduler s;
    TEST_ASSERT_TRUE(s.takeSampleNow(7000));
    s.restart(7001);
    TEST_ASSERT_TRUE(s.takeSampleNow(7001));
}

void test_AC4_restart_sampling_window_is_first_800ms_only() {
    PeriodScheduler s;
    warmUp(s, 1950);  // old window state: inside the old period's no-sample zone
    const uint32_t t0 = 1950;
    s.restart(t0);
    TEST_ASSERT_TRUE(s.takeSampleNow(t0));
    int count = 1;
    for (uint32_t t = t0 + 1; t < t0 + 1000; t++) {
        bool take = s.takeSampleNow(t);
        if (t >= t0 + 800) TEST_ASSERT_FALSE(take);
        if (take) count++;
    }
    TEST_ASSERT_EQUAL_INT(400, count);
}

void test_AC4_restart_sample_spacing_restarts_at_2ms() {
    PeriodScheduler s;
    warmUp(s, 3333);
    const uint32_t t0 = 3333;
    s.restart(t0);
    TEST_ASSERT_TRUE(s.takeSampleNow(t0));
    TEST_ASSERT_FALSE(s.takeSampleNow(t0 + 1));
    TEST_ASSERT_TRUE(s.takeSampleNow(t0 + 2));
    TEST_ASSERT_FALSE(s.takeSampleNow(t0 + 3));
    TEST_ASSERT_TRUE(s.takeSampleNow(t0 + 4));
}

void test_AC4_restart_second_period_after_restart_also_aligned() {
    PeriodScheduler s;
    warmUp(s, 1234);
    const uint32_t t0 = 1234;
    s.restart(t0);
    int inWindow = 0;
    for (uint32_t t = t0; t < t0 + 2000; t++) {
        bool take = s.takeSampleNow(t);
        s.periodEnded(t);
        if (t >= t0 + 1000 && take) {
            TEST_ASSERT_TRUE_MESSAGE(t < t0 + 1800, "sample after 800 ms of second period");
            inWindow++;
        }
        if (t >= t0 + 800 && t < t0 + 1000) TEST_ASSERT_FALSE(take);
    }
    TEST_ASSERT_EQUAL_INT(400, inWindow);
}

void test_AC4_without_restart_origin_is_first_call() {
    PeriodScheduler s;
    TEST_ASSERT_TRUE(s.takeSampleNow(5000));
    for (uint32_t t = 5001; t < 6000; t++) TEST_ASSERT_FALSE(s.periodEnded(t));
    TEST_ASSERT_TRUE(s.periodEnded(6000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_AC12_0250V_is_25C);
    RUN_TEST(test_AC12_0550V_is_55C);
    RUN_TEST(test_AC12_0600V_is_60C);
    RUN_TEST(test_AC12_0650V_is_65C);
    RUN_TEST(test_AC12_0800V_is_80C);
    RUN_TEST(test_AC14_window_mean_of_injected_samples);
    RUN_TEST(test_AC14_mean_is_fractional_not_truncated);
    RUN_TEST(test_AC14_mean_of_many_full_scale_samples_does_not_overflow);
    RUN_TEST(test_AC14_no_values_from_earlier_period_mixed_in);
    RUN_TEST(test_AC14_empty_period_has_zero_samples_and_holds_previous_values);
    RUN_TEST(test_AC14_takeSampleNow_only_in_first_800ms_at_2ms_spacing);
    RUN_TEST(test_AC14_no_sample_before_2ms_elapsed);
    RUN_TEST(test_AC14_exactly_one_period_end_per_1000ms);
    RUN_TEST(test_AC14_sampling_window_repeats_in_second_period);
    RUN_TEST(test_AC4_restart_period_ends_exactly_once_at_t_plus_1000);
    RUN_TEST(test_AC4_restart_periodEnded_false_before_t_plus_1000_true_at_it);
    RUN_TEST(test_AC4_restart_sample_allowed_at_t_even_if_sampled_1ms_before);
    RUN_TEST(test_AC4_restart_sampling_window_is_first_800ms_only);
    RUN_TEST(test_AC4_restart_sample_spacing_restarts_at_2ms);
    RUN_TEST(test_AC4_restart_second_period_after_restart_also_aligned);
    RUN_TEST(test_AC4_without_restart_origin_is_first_call);
    return UNITY_END();
}
