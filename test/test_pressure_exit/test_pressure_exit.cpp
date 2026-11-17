// Unit tests: "pressure >= X" phase exits ignore pressure trapped in the boiler from the previous shot
// (display/models/pressure_exit.h). Host-side: pio test -e native -f test_pressure_exit.

#include <unity.h>

#include <algorithm>
#include <cmath>

#include <display/models/pressure_exit.h>

void setUp() {}
void tearDown() {}

// Feeds pressure readings to a "fill until target bar" phase the way BrewProcess does (lowest reading so far, then
// the check) and returns the index of the reading that ended it, or -1 if none did.
static int exitIndex(const float *readings, int count, float target) {
    float lowest = INFINITY;
    for (int i = 0; i < count; i++) {
        lowest = std::min(lowest, readings[i]);
        if (pressureRiseReached(target, readings[i], lowest))
            return i;
    }
    return -1;
}

// Logged on a Silvia, 2026-11-14 09:11 (Silvia Daily, Fill exits at 2.5 bar): 3.2 bar trapped when Start was pressed,
// released into the puck within a second, then the pump built it back up. Before the fix the Fill ended on the first
// reading and the shot went straight to Soak.
static const float kTrappedStart[] = {3.235f, 1.358f, 0.321f, 0.249f, 0.278f, 0.308f, 0.376f, 0.521f,
                                      0.749f, 1.04f,  1.387f, 1.88f,  2.346f, 2.618f, 2.76f};

void test_trapped_pressure_does_not_end_the_fill_at_once() {
    TEST_ASSERT_EQUAL_INT(13, exitIndex(kTrappedStart, 15, 2.5f)); // 2.618 bar, built by the pump
}

void test_shot_without_trapped_pressure_is_unchanged() {
    const float readings[] = {0.05f, 0.4f, 1.2f, 2.1f, 2.6f};
    TEST_ASSERT_EQUAL_INT(4, exitIndex(readings, 5, 2.5f));
}

void test_later_phase_targets_still_arm() {
    // After the release the lowest reading is ~0.25 bar, so a later 8.5 bar ramp target ends as soon as it's reached,
    // even when that phase starts above an earlier phase's target.
    TEST_ASSERT_TRUE(pressureRiseReached(8.5f, 8.6f, 0.249f));
    TEST_ASSERT_FALSE(pressureRiseReached(8.5f, 8.4f, 0.249f));
}

void test_target_above_the_trapped_pressure_is_unchanged() {
    // 9 bar target (Sir Lancelot's Lever) with 6.8 bar trapped: already below it, so it counts when reached.
    TEST_ASSERT_TRUE(pressureRiseReached(9.0f, 9.0f, 6.8f));
}

void test_untracked_lowest_keeps_old_behaviour() {
    // Phase::isFinished defaults lowest_pressure to 0 for callers that don't track it.
    TEST_ASSERT_TRUE(pressureRiseReached(2.5f, 3.2f, 0.0f));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_trapped_pressure_does_not_end_the_fill_at_once);
    RUN_TEST(test_shot_without_trapped_pressure_is_unchanged);
    RUN_TEST(test_later_phase_targets_still_arm);
    RUN_TEST(test_target_above_the_trapped_pressure_is_unchanged);
    RUN_TEST(test_untracked_lowest_keeps_old_behaviour);
    return UNITY_END();
}
