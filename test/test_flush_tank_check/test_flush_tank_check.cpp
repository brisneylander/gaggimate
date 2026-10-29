// Unit tests: tank temperature implied by a flush. Host-side — pio test -e native -f test_flush_tank_check.

#include <unity.h>

#include <cstdio>

#include "bench_flushes.h"

using namespace flush_tank_check;

void setUp() {}
void tearDown() {}

// Real flushes with the tank measured at 32 °C: within the per-flush noise (the bench spread was ±7 °C).
void test_bench_flushes_imply_about_the_measured_tank() {
    const Plant p;
    for (const auto *s : {&WEIGHED_10S, &WEIGHED_5S, &AB_FLUSH}) {
        float rms = 0.0f;
        const float implied = impliedTank(*s, p, &rms);
        printf("implied tank %.1f °C (fit rms %.2f)\n", implied, rms);
        TEST_ASSERT_TRUE(std::isfinite(implied));
        TEST_ASSERT_FLOAT_WITHIN(8.0f, 32.0f, implied);
    }
}

// A replay of the plant itself at 20 °C is recovered (less the bias the caller adds back).
void test_recovers_a_synthetic_tank() {
    const Plant p;
    std::vector<Sample> s;
    // Build the response by stepping the same plant: hold, a 5 s flush at 8.9 ml/s, then the heater at 30 % for a while.
    for (int i = 0; i < 181; ++i)
        s.push_back({93.0f, i < 40 && i > 5 ? 0.3f : 0.04f, i >= 2 && i < 7 ? 8.9f : 0.0f});
    // Find the readings that make 20 °C the exact answer: iterate the replay's own output.
    std::vector<Sample> truth = s;
    {
        // A replay against flat readings gives the model's own trace; take it as the "measured" one.
        const float elementC = 0.109f * p.capacity, waterC = 0.707f * p.capacity, groupC = 0.447f * p.capacity;
        const float toWaterPerC = elementC / 20.9f, toGroupPerC = groupC / 143.6f;
        float water = 93.0f, element = 93.0f + p.holdW / toWaterPerC, group = 93.0f - p.holdW / toGroupPerC, lag = 93.0f;
        const float lossPerC = p.holdW / (93.0f - 22.0f);
        std::vector<float> pipe(18, 93.0f);
        size_t head = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            truth[i].temp = lag;
            for (int k = 0; k < 4; ++k) {
                const float toWater = toWaterPerC * (element - water), toGroup = toGroupPerC * (water - group);
                element += (p.heaterW * s[i].duty - toWater) * 0.25f / elementC;
                group += (toGroup - lossPerC * (water - 22.0f)) * 0.25f / groupC;
                water += (toWater - s[i].flow * 4.18f * (water - 20.0f) - toGroup) * 0.25f / waterC;
                const float delayed = pipe[head];
                pipe[head] = water;
                head = (head + 1) % pipe.size();
                lag += (delayed - lag) * 0.25f / 15.0f;
            }
        }
    }
    float rms = 0.0f;
    const float implied = impliedTank(truth, p, &rms);
    TEST_ASSERT_FLOAT_WITHIN(0.6f, 20.0f + BIAS_C, implied);
    TEST_ASSERT_TRUE(rms < 0.05f);
}

void test_poor_fit_gives_no_verdict() {
    const Plant p;
    std::vector<Sample> s;
    for (int i = 0; i < 181; ++i)
        s.push_back({93.0f + (i % 20 < 10 ? 4.0f : -4.0f), 0.04f, 0.0f}); // nothing a flush explains
    TEST_ASSERT_TRUE(std::isnan(impliedTank(s, p)));
}

// Two flushes in a row well below the estimate are a refill; one, or a warmer one between, is not.
void test_refill_needs_two_cold_flushes_in_a_row() {
    RefillDetector d;
    TEST_ASSERT_FALSE(d.onFlush(20.0f, 35.0f));
    TEST_ASSERT_FALSE(d.onFlush(33.0f, 35.0f)); // resets
    TEST_ASSERT_FALSE(d.onFlush(20.0f, 35.0f));
    TEST_ASSERT_FALSE(d.onFlush(NAN, 35.0f)); // no verdict keeps the count
    TEST_ASSERT_TRUE(d.onFlush(22.0f, 35.0f));
    TEST_ASSERT_FALSE(d.onFlush(22.0f, 35.0f)); // counting starts again
}

// The bench flushes (32 °C tank) never look like a refill against a correct estimate.
void test_no_false_refill_on_bench_flushes() {
    const Plant p;
    RefillDetector d;
    for (const auto *s : {&WEIGHED_10S, &WEIGHED_5S, &AB_FLUSH, &WEIGHED_10S})
        TEST_ASSERT_FALSE(d.onFlush(impliedTank(*s, p), 32.0f));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_bench_flushes_imply_about_the_measured_tank);
    RUN_TEST(test_recovers_a_synthetic_tank);
    RUN_TEST(test_poor_fit_gives_no_verdict);
    RUN_TEST(test_refill_needs_two_cold_flushes_in_a_row);
    RUN_TEST(test_no_false_refill_on_bench_flushes);
    return UNITY_END();
}
