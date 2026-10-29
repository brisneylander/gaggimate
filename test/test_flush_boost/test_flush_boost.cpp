#include <unity.h>

#include "../../src/display/core/FlushBoost.h"

using flush_boost::Action;
using flush_boost::Boost;

static constexpr float TARGET = 93.0f;
static constexpr float SURPLUS = 5.0f;

void setUp() {}
void tearDown() {}

void test_idle_until_started() {
    Boost b;
    TEST_ASSERT_FALSE(b.isActive());
    TEST_ASSERT_EQUAL(Action::None, b.step(0, 99.0f, TARGET, SURPLUS));
}

// The flush's own dip and the climb back don't fire; reaching target + surplus does.
void test_flushes_once_the_surplus_is_reached() {
    Boost b;
    b.start(1000);
    const float climb[] = {93.0f, 90.0f, 88.5f, 89.0f, 92.0f, 95.0f, 97.5f};
    unsigned long t = 1000;
    for (float r : climb)
        TEST_ASSERT_EQUAL(Action::None, b.step(t += 1000, r, TARGET, SURPLUS));
    TEST_ASSERT_EQUAL(Action::Flush, b.step(t += 1000, 97.8f, TARGET, SURPLUS));
    TEST_ASSERT_FALSE(b.isActive());
    TEST_ASSERT_EQUAL(Action::None, b.step(t += 1000, 98.0f, TARGET, SURPLUS));
}

// The PID settles short of target + surplus: flush once the reading turns down above the target.
void test_flushes_at_a_peak_short_of_the_surplus() {
    Boost b;
    b.start(0);
    TEST_ASSERT_EQUAL(Action::None, b.step(1000, 95.0f, TARGET, SURPLUS));
    TEST_ASSERT_EQUAL(Action::None, b.step(2000, 96.1f, TARGET, SURPLUS));
    TEST_ASSERT_EQUAL(Action::None, b.step(3000, 96.0f, TARGET, SURPLUS)); // noise, not a peak
    TEST_ASSERT_EQUAL(Action::Flush, b.step(4000, 95.85f, TARGET, SURPLUS));
}

// A wobble in the flush's dip, below target, is not a peak.
void test_no_flush_for_a_peak_below_target() {
    Boost b;
    b.start(0);
    TEST_ASSERT_EQUAL(Action::None, b.step(1000, 92.0f, TARGET, SURPLUS));
    TEST_ASSERT_EQUAL(Action::None, b.step(2000, 91.0f, TARGET, SURPLUS));
    TEST_ASSERT_TRUE(b.isActive());
}

void test_gives_up_after_the_time_limit() {
    Boost b;
    b.start(500);
    TEST_ASSERT_EQUAL(Action::None, b.step(500 + flush_boost::MAX_MS, 93.4f, TARGET, SURPLUS));
    TEST_ASSERT_EQUAL(Action::GiveUp, b.step(501 + flush_boost::MAX_MS, 93.4f, TARGET, SURPLUS));
    TEST_ASSERT_FALSE(b.isActive());
}

// millis() wraps after ~49 days; elapsed time is unsigned subtraction.
void test_survives_millis_wrap() {
    Boost b;
    b.start(0xFFFFF000UL);
    TEST_ASSERT_EQUAL(Action::None, b.step(0x00001000UL, 92.0f, TARGET, SURPLUS));
    TEST_ASSERT_TRUE(b.isActive());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_until_started);
    RUN_TEST(test_flushes_once_the_surplus_is_reached);
    RUN_TEST(test_flushes_at_a_peak_short_of_the_surplus);
    RUN_TEST(test_no_flush_for_a_peak_below_target);
    RUN_TEST(test_gives_up_after_the_time_limit);
    RUN_TEST(test_survives_millis_wrap);
    return UNITY_END();
}
