// Unit tests: "flush to target" volume. Host-side — pio test -e native -f test_flush_to_target.

#include <unity.h>

#include "display/core/FlushToTarget.h"

using flush_to_target::plan;

void setUp() {}
void tearDown() {}

// Silvia on the bench: 1100 W at 0.6 °C/s (1833 J/°C), +5 °C display offset, 30 °C tank, 8.644 ml/s into an open group.
constexpr float W = 1100.0f, RATE = 0.6f, OFFSET = 5.0f, TANK = 30.0f, FLOW = 8.644f;
constexpr float NO_LOSS = 1e-6f; // isolates the heat sums from the standing-loss allowance

void test_two_degrees_over_needs_a_short_flush() {
    const auto p = plan(95.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW, NO_LOSS);
    TEST_ASSERT_NULL(p.reason);
    // 2 °C x 0.85 realised x 1833 J/°C / (4.18 x (99 - 30)) = 10.8 ml
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 12.7f * flush_to_target::SURPLUS_REALISED, p.ml);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.7f * flush_to_target::SURPLUS_REALISED / FLOW, p.seconds);
}

// Straight after steaming: 32 °C x 0.85 x 1833 J/°C / (4.18 x (114 - 30)) = 142 ml, about 16 s.
void test_after_steam() {
    const auto p = plan(125.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW, NO_LOSS);
    TEST_ASSERT_NULL(p.reason);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 167.0f * flush_to_target::SURPLUS_REALISED, p.ml);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 19.3f * flush_to_target::SURPLUS_REALISED, p.seconds);
}

// A surplus needing more than 30 s (here a 70 °C tank) is capped per press; press again if still hot.
void test_long_flush_is_capped() {
    const auto p = plan(125.0f, 93.0f, OFFSET, W, RATE, 70.0f, FLOW, NO_LOSS);
    TEST_ASSERT_NULL(p.reason);
    TEST_ASSERT_EQUAL_FLOAT(flush_to_target::MAX_SECONDS, p.seconds);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, flush_to_target::MAX_SECONDS * FLOW, p.ml);
}

// Warmer tank water cools less per ml, so it needs more.
void test_warmer_tank_needs_more_water() {
    const auto cool = plan(97.0f, 93.0f, OFFSET, W, RATE, 23.0f, FLOW, NO_LOSS);
    const auto warm = plan(97.0f, 93.0f, OFFSET, W, RATE, 40.0f, FLOW, NO_LOSS);
    TEST_ASSERT_TRUE(warm.ml > cool.ml);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (100.0f - 23.0f) / (100.0f - 40.0f), warm.ml / cool.ml);
}

void test_tank_zero_means_default() {
    const auto zero = plan(97.0f, 93.0f, OFFSET, W, RATE, 0.0f, FLOW, NO_LOSS);
    const auto dflt = plan(97.0f, 93.0f, OFFSET, W, RATE, flush_to_target::DEFAULT_INLET_TEMP_C, FLOW, NO_LOSS);
    TEST_ASSERT_EQUAL_FLOAT(dflt.ml, zero.ml);
}

void test_refuses_when_not_needed_or_not_configured() {
    TEST_ASSERT_NOT_NULL(plan(93.3f, 93.0f, OFFSET, W, RATE, TANK, FLOW, NO_LOSS).reason);    // within 0.5 °C
    TEST_ASSERT_NOT_NULL(plan(92.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW, NO_LOSS).reason);    // below target
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, 0.0f, RATE, TANK, FLOW, NO_LOSS).reason); // no heater power
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, W, 0.0f, TANK, FLOW, NO_LOSS).reason);    // no heat-up rate
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, W, RATE, TANK, 0.0f, NO_LOSS).reason);    // no pump flow
    TEST_ASSERT_NOT_NULL(plan(96.0f, 93.0f, OFFSET, W, RATE, 95.0f, FLOW, NO_LOSS).reason);   // tank as hot as the boiler
    const auto none = plan(92.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW, NO_LOSS);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, none.ml);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, none.seconds);
}

// The boiler keeps losing heat through the flush and the heater hold, so less needs flushing: on the second bench run
// (106.5 -> 93 °C) ignoring it undershot by ~2 °C.
void test_standing_loss_reduces_the_flush() {
    const auto none = plan(106.5f, 93.0f, OFFSET, W, RATE, 35.0f, FLOW, NO_LOSS, 30.0f);
    const auto loss = plan(106.5f, 93.0f, OFFSET, W, RATE, 35.0f, FLOW, 37.0f, 30.0f);
    TEST_ASSERT_NULL(loss.reason);
    // 37 W over the ~9 s flush + 30 s hold = ~1.45 kJ less of a 24.7 kJ surplus: ~6 % less water.
    const float perMl = 4.18f * ((106.5f + 93.0f) / 2.0f + OFFSET - 35.0f);
    const float expectedMl = (13.5f * flush_to_target::SURPLUS_REALISED * W / RATE - 37.0f * (loss.seconds + 30.0f)) / perMl;
    TEST_ASSERT_FLOAT_WITHIN(0.3f, expectedMl, loss.ml);
    TEST_ASSERT_TRUE(loss.ml < none.ml);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.93f, loss.ml / none.ml);
}

// No learned loss yet: falls back to 5 % of the heater, like the predictor.
void test_unlearned_loss_uses_default_fraction() {
    const auto dflt = plan(100.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW, 0.0f, 30.0f);
    const auto five = plan(100.0f, 93.0f, OFFSET, W, RATE, TANK, FLOW, 0.05f * W, 30.0f);
    TEST_ASSERT_EQUAL_FLOAT(five.ml, dflt.ml);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_two_degrees_over_needs_a_short_flush);
    RUN_TEST(test_after_steam);
    RUN_TEST(test_long_flush_is_capped);
    RUN_TEST(test_warmer_tank_needs_more_water);
    RUN_TEST(test_tank_zero_means_default);
    RUN_TEST(test_refuses_when_not_needed_or_not_configured);
    RUN_TEST(test_standing_loss_reduces_the_flush);
    RUN_TEST(test_unlearned_loss_uses_default_fraction);
    return UNITY_END();
}
