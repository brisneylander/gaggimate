// Unit tests: "flush to target" volume. Host-side — pio test -e native -f test_flush_to_target.

#include <unity.h>

#include "display/core/FlushToTarget.h"

using flush_to_target::plan;

void setUp() {}
void tearDown() {}

// Silvia on the bench: 1100 W at 0.6 °C/s (1833 J/°C), +5 °C display offset, 30 °C tank, 8.644 ml/s into an open group.
constexpr float W = 1100.0f, RATE = 0.6f, OFFSET = 5.0f, TANK = 30.0f, FLOW = 8.644f;

void test_two_degrees_over_needs_a_short_flush() {
    const auto p = plan(95.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW);
    TEST_ASSERT_NULL(p.reason);
    // 2 °C x 1833 J/°C / (4.18 x (99 - 30)) = 12.7 ml
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 12.7f, p.ml);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.7f / FLOW, p.seconds);
}

// Straight after steaming: 32 °C x 1833 J/°C / (4.18 x (114 - 30)) = 167 ml, about 19 s.
void test_after_steam() {
    const auto p = plan(125.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW);
    TEST_ASSERT_NULL(p.reason);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 167.0f, p.ml);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 19.3f, p.seconds);
}

// A surplus needing more than 30 s (here a 60 °C tank) is capped per press; press again if still hot.
void test_long_flush_is_capped() {
    const auto p = plan(125.0f, 93.0f, OFFSET, W, RATE, 60.0f, FLOW);
    TEST_ASSERT_NULL(p.reason);
    TEST_ASSERT_EQUAL_FLOAT(flush_to_target::MAX_SECONDS, p.seconds);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, flush_to_target::MAX_SECONDS * FLOW, p.ml);
}

// Warmer tank water cools less per ml, so it needs more.
void test_warmer_tank_needs_more_water() {
    const auto cool = plan(97.0f, 93.0f, OFFSET, W, RATE, 23.0f, FLOW);
    const auto warm = plan(97.0f, 93.0f, OFFSET, W, RATE, 40.0f, FLOW);
    TEST_ASSERT_TRUE(warm.ml > cool.ml);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (100.0f - 23.0f) / (100.0f - 40.0f), warm.ml / cool.ml);
}

void test_tank_zero_means_default() {
    const auto zero = plan(97.0f, 93.0f, OFFSET, W, RATE, 0.0f, FLOW);
    const auto dflt = plan(97.0f, 93.0f, OFFSET, W, RATE, flush_to_target::DEFAULT_INLET_TEMP_C, FLOW);
    TEST_ASSERT_EQUAL_FLOAT(dflt.ml, zero.ml);
}

void test_refuses_when_not_needed_or_not_configured() {
    TEST_ASSERT_NOT_NULL(plan(93.3f, 93.0f, OFFSET, W, RATE, TANK, FLOW).reason);    // within 0.5 °C
    TEST_ASSERT_NOT_NULL(plan(92.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW).reason);    // below target
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, 0.0f, RATE, TANK, FLOW).reason); // no heater power
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, W, 0.0f, TANK, FLOW).reason);    // no heat-up rate
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, W, RATE, TANK, 0.0f).reason);    // no pump flow
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, W, RATE, 95.0f, FLOW).reason);   // tank as hot as the boiler
    const auto none = plan(92.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, none.ml);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, none.seconds);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_two_degrees_over_needs_a_short_flush);
    RUN_TEST(test_after_steam);
    RUN_TEST(test_long_flush_is_capped);
    RUN_TEST(test_warmer_tank_needs_more_water);
    RUN_TEST(test_tank_zero_means_default);
    RUN_TEST(test_refuses_when_not_needed_or_not_configured);
    return UNITY_END();
}
