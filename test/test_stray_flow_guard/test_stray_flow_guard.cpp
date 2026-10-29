// Unit tests: re-sending pump off when flow is reported with nothing running. Host-side —
// pio test -e native -f test_stray_flow_guard.

#include <unity.h>

#include "display/core/StrayFlowGuard.h"

void setUp() {}
void tearDown() {}

// The bench case: 5 ml/s reported after the brew stopped. One re-send after 3 s, then one every 3 s while it lasts.
void test_resends_while_flow_outlasts_the_stop() {
    StrayFlowGuard g;
    TEST_ASSERT_FALSE(g.check(10000, false, 5.0f));
    TEST_ASSERT_FALSE(g.check(12000, false, 5.0f));
    TEST_ASSERT_TRUE(g.check(13000, false, 5.0f));
    TEST_ASSERT_FALSE(g.check(15000, false, 5.0f));
    TEST_ASSERT_TRUE(g.check(16000, false, 5.0f));
}

// The pump's filtered flow winds down within about a second of a normal stop: no re-send.
void test_normal_wind_down_is_left_alone() {
    StrayFlowGuard g;
    TEST_ASSERT_FALSE(g.check(10000, false, 3.9f));
    TEST_ASSERT_FALSE(g.check(10500, false, 1.2f));
    TEST_ASSERT_FALSE(g.check(11000, false, 0.36f));
    TEST_ASSERT_FALSE(g.check(20000, false, 0.0f));
}

// Flow during a shot, flush, hot water or steam is expected.
void test_running_process_is_left_alone() {
    StrayFlowGuard g;
    for (unsigned long t = 0; t < 60000; t += 1000)
        TEST_ASSERT_FALSE(g.check(t, true, 8.6f));
}

// Flow that stops resets the timer.
void test_gap_resets_the_timer() {
    StrayFlowGuard g;
    TEST_ASSERT_FALSE(g.check(1000, false, 5.0f));
    TEST_ASSERT_FALSE(g.check(3500, false, 0.0f));
    TEST_ASSERT_FALSE(g.check(4000, false, 5.0f));
    TEST_ASSERT_FALSE(g.check(6500, false, 5.0f));
    TEST_ASSERT_TRUE(g.check(7000, false, 5.0f));
}

// millis() wraps after ~49 days.
void test_survives_millis_wrap() {
    StrayFlowGuard g;
    TEST_ASSERT_FALSE(g.check(0xFFFFF000UL, false, 5.0f));
    TEST_ASSERT_TRUE(g.check(0x00000800UL, false, 5.0f));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_resends_while_flow_outlasts_the_stop);
    RUN_TEST(test_normal_wind_down_is_left_alone);
    RUN_TEST(test_running_process_is_left_alone);
    RUN_TEST(test_gap_resets_the_timer);
    RUN_TEST(test_survives_millis_wrap);
    return UNITY_END();
}
