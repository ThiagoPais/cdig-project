#include <unity.h>

void setUp() {}
void tearDown() {}

void test_harness_runs() {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_harness_runs);
    return UNITY_END();
}
