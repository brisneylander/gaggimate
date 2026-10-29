// Unit tests: hard limits on the heater. Host-side — pio test -e native -f test_heater_safety.

#include <unity.h>

#include "peripherals/HeaterSafety.h"

using Reason = HeaterSafety::Reason;

void setUp() {}
void tearDown() {}

// The failure it exists for: the thermocouple comes off the boiler at idle and reads room temperature, so the PID heats
// flat out. Trips after 60 s at brew temperatures, and stays tripped.
void test_detached_sensor_at_brew_trips() {
    HeaterSafety s;
    uint32_t t = 0;
    for (; t < 59000; t += 1000)
        TEST_ASSERT_TRUE(s.observe(t, 25.0f, 1.0f, false, 98.0f));
    TEST_ASSERT_FALSE(s.observe(t + 1000, 25.0f, 1.0f, false, 98.0f));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::NoRise), static_cast<int>(s.reason()));
    TEST_ASSERT_FALSE(s.permits());
    TEST_ASSERT_FALSE(s.observe(t + 2000, 95.0f, 0.0f, false, 98.0f)); // latched until a restart
}

// A cold start: full power, the sensor reacts ~20 s late, then rises ~0.6 °C/s. No trip, up to steam temperature.
void test_cold_start_to_steam_does_not_trip() {
    HeaterSafety s;
    float temp = 22.0f;
    for (uint32_t t = 0; t < 230000; t += 1000) {
        if (t > 20000 && temp < 145.0f)
            temp += 0.6f; // then holds at steam temperature: the PID would drop below full power, but this keeps it flat out
        TEST_ASSERT_TRUE(s.observe(t, temp, 1.0f, false, 145.0f));
    }
}

// A flush or shot: full power while water flows and the reading falls; then the climb back.
void test_flush_does_not_trip() {
    HeaterSafety s;
    float temp = 93.0f;
    uint32_t t = 0;
    for (; t < 40000; t += 1000) {
        temp -= 0.15f;
        TEST_ASSERT_TRUE(s.observe(t, temp, 1.0f, true, 98.0f));
    }
    for (; t < 70000; t += 1000)
        TEST_ASSERT_TRUE(s.observe(t, temp, 1.0f, false, 98.0f)); // heat on its way: 30 s flat
    for (; t < 100000; t += 1000) {
        temp += 0.3f;
        TEST_ASSERT_TRUE(s.observe(t, temp, 1.0f, false, 98.0f));
    }
}

// Steaming milk: flat out for 90 s while the reading falls from 140 to 125; no trip at steam temperatures.
void test_steaming_milk_does_not_trip() {
    HeaterSafety s;
    float temp = 140.0f;
    for (uint32_t t = 0; t < 90000; t += 1000) {
        temp -= 0.17f;
        TEST_ASSERT_TRUE(s.observe(t, temp, 1.0f, false, 145.0f));
    }
}

// ... but a detached sensor in steam mode still trips, after 180 s.
void test_detached_sensor_at_steam_trips() {
    HeaterSafety s;
    uint32_t t = 0;
    for (; t < 179000; t += 1000)
        TEST_ASSERT_TRUE(s.observe(t, 30.0f, 1.0f, false, 145.0f));
    TEST_ASSERT_FALSE(s.observe(t + 1000, 30.0f, 1.0f, false, 145.0f));
}

// Holding at low duty never counts as "full power, no rise".
void test_holding_does_not_trip() {
    HeaterSafety s;
    for (uint32_t t = 0; t < 3600000; t += 1000)
        TEST_ASSERT_TRUE(s.observe(t, 93.0f, 0.05f, false, 98.0f));
}

// One glitch, or two, is left to the sensor's own error counting; three within 30 s trips.
void test_jumps() {
    HeaterSafety s;
    uint32_t t = 0;
    TEST_ASSERT_TRUE(s.observe(t, 93.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_TRUE(s.observe(t += 1000, 110.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_TRUE(s.observe(t += 1000, 93.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_FALSE(s.observe(t += 1000, 120.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::Jumps), static_cast<int>(s.reason()));
}

void test_spaced_jumps_do_not_trip() {
    HeaterSafety s;
    uint32_t t = 0;
    float temp = 93.0f;
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE(s.observe(t += 1000, temp, 0.05f, false, 98.0f));
        temp = temp > 100.0f ? 93.0f : 110.0f; // one jump ...
        TEST_ASSERT_TRUE(s.observe(t += 1000, temp, 0.05f, false, 98.0f));
        for (int k = 0; k < 20; ++k) // ... every 21 s: at most two in any 30 s
            TEST_ASSERT_TRUE(s.observe(t += 1000, temp, 0.05f, false, 98.0f));
    }
}

// Full power for 10 minutes straight, whatever the reading does.
void test_full_power_too_long() {
    HeaterSafety s;
    float temp = 22.0f;
    uint32_t t = 0;
    for (; t < 599000; t += 1000) {
        // Rising 2.1 °C every 100 s, then back: never "no rise", never over the limit.
        temp = 100.0f + static_cast<float>((t / 1000) % 100) * 0.021f;
        TEST_ASSERT_TRUE(s.observe(t, temp, 1.0f, false, 300.0f));
    }
    TEST_ASSERT_FALSE(s.observe(t + 1000, temp, 1.0f, false, 300.0f));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::FullPowerTooLong), static_cast<int>(s.reason()));
}

// A gap in readings (sensor task stalled) isn't a jump.
void test_reading_gap_is_not_a_jump() {
    HeaterSafety s;
    TEST_ASSERT_TRUE(s.observe(0, 93.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_TRUE(s.observe(10000, 120.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_TRUE(s.observe(11000, 120.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_TRUE(s.permits());
}

void test_over_temperature() {
    HeaterSafety s;
    TEST_ASSERT_TRUE(s.observe(0, 165.0f, 0.5f, false, 145.0f));
    TEST_ASSERT_FALSE(s.observe(1000, 171.0f, 0.5f, false, 145.0f));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::OverTemp), static_cast<int>(s.reason()));
}

// A sensor in its error state for 3 steps after a good reading trips; at start-up (no good reading yet) it doesn't.
void test_sensor_fault() {
    HeaterSafety boot;
    for (uint32_t t = 0; t < 10000; t += 1000)
        TEST_ASSERT_TRUE(boot.observe(t, 0.0f, 0.0f, false, 98.0f, true));
    HeaterSafety s;
    TEST_ASSERT_TRUE(s.observe(0, 93.0f, 0.05f, false, 98.0f));
    TEST_ASSERT_TRUE(s.observe(1000, 0.0f, 0.05f, false, 98.0f, true));
    TEST_ASSERT_TRUE(s.observe(2000, 0.0f, 0.05f, false, 98.0f, true));
    TEST_ASSERT_TRUE(s.observe(3000, 93.0f, 0.05f, false, 98.0f)); // recovered: count resets
    TEST_ASSERT_TRUE(s.observe(4000, 0.0f, 0.05f, false, 98.0f, true));
    TEST_ASSERT_TRUE(s.observe(5000, 0.0f, 0.05f, false, 98.0f, true));
    TEST_ASSERT_FALSE(s.observe(6000, 0.0f, 0.05f, false, 98.0f, true));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::SensorFault), static_cast<int>(s.reason()));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_detached_sensor_at_brew_trips);
    RUN_TEST(test_cold_start_to_steam_does_not_trip);
    RUN_TEST(test_flush_does_not_trip);
    RUN_TEST(test_steaming_milk_does_not_trip);
    RUN_TEST(test_detached_sensor_at_steam_trips);
    RUN_TEST(test_holding_does_not_trip);
    RUN_TEST(test_jumps);
    RUN_TEST(test_spaced_jumps_do_not_trip);
    RUN_TEST(test_full_power_too_long);
    RUN_TEST(test_reading_gap_is_not_a_jump);
    RUN_TEST(test_over_temperature);
    RUN_TEST(test_sensor_fault);
    return UNITY_END();
}
