// Unit tests: tank water temperature estimate. Host-side — pio test -e native -f test_tank_temperature.

#include <unity.h>

#include "display/core/TankTemperature.h"

void setUp() {}
void tearDown() {}

static void run(TankTemperature &t, float seconds, bool on) {
    for (float s = 0.0f; s < seconds; s += 10.0f)
        t.update(10.0f, on);
}

void test_off_until_a_fill_temperature_is_set() {
    TankTemperature t;
    TEST_ASSERT_FALSE(t.isEnabled());
    t.configure(0.0f);
    TEST_ASSERT_FALSE(t.isEnabled());
    TEST_ASSERT_FALSE(t.onWaterLevel(50));
    TEST_ASSERT_FALSE(t.onWaterLevel(100));
}

// Fresh 25 °C water, machine on: ~32 °C after an hour, ~39 °C after three (the bench saw ~40 after a few hours).
void test_warms_while_on() {
    TankTemperature t;
    t.configure(25.0f);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, t.get());
    run(t, 3600.0f, true);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 32.1f, t.get());
    run(t, 7200.0f, true);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 39.0f, t.get());
}

void test_cools_in_standby() {
    TankTemperature t;
    t.configure(20.0f);
    run(t, 4 * 3600.0f, true);
    const float warm = t.get();
    run(t, 3 * 3600.0f, false);
    TEST_ASSERT_TRUE(t.get() < warm - 5.0f);
    TEST_ASSERT_TRUE(t.get() > 20.0f);
}

void test_refill_by_hand() {
    TankTemperature t;
    t.configure(18.0f);
    run(t, 3600.0f, true);
    TEST_ASSERT_TRUE(t.get() > 20.0f);
    t.refilled();
    TEST_ASSERT_EQUAL_FLOAT(18.0f, t.get());
}

// The level sensor counts a refill only when it climbs back to full after being low; noise near full doesn't.
void test_refill_from_the_water_level() {
    TankTemperature t;
    t.configure(18.0f);
    run(t, 3600.0f, true);
    TEST_ASSERT_FALSE(t.onWaterLevel(100));
    TEST_ASSERT_FALSE(t.onWaterLevel(95));
    TEST_ASSERT_FALSE(t.onWaterLevel(100));
    TEST_ASSERT_TRUE(t.get() > 20.0f);
    TEST_ASSERT_FALSE(t.onWaterLevel(60));
    TEST_ASSERT_FALSE(t.onWaterLevel(80));
    TEST_ASSERT_TRUE(t.onWaterLevel(100));
    TEST_ASSERT_EQUAL_FLOAT(18.0f, t.get());
    TEST_ASSERT_FALSE(t.onWaterLevel(100)); // once per refill
}

// A new season's fill temperature keeps how far the tank had warmed.
void test_new_fill_temperature_keeps_the_warming() {
    TankTemperature t;
    t.configure(25.0f);
    run(t, 3600.0f, true);
    const float warmed = t.get() - 25.0f;
    t.configure(16.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.0f + warmed, t.get());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_off_until_a_fill_temperature_is_set);
    RUN_TEST(test_warms_while_on);
    RUN_TEST(test_cools_in_standby);
    RUN_TEST(test_refill_by_hand);
    RUN_TEST(test_refill_from_the_water_level);
    RUN_TEST(test_new_fill_temperature_keeps_the_warming);
    return UNITY_END();
}
