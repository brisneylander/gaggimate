// Unit tests: HeatBalancePredictor, plus a closed-loop flush replay of a single-boiler machine with a
// laggy thermocouple (Rancilio Silvia class: 1100 W element, ~1.8 kJ/°C, ~20 s heater-to-sensor lag).
// Host-side, no ESP32/Arduino runtime — pio test -e native -f test_heat_balance_predictor.

#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>

static uint32_t fakeNow = 0;
static uint32_t millis() { return fakeNow; }
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define ESP_LOGV(...)
#ifndef PI
#define PI 3.14159265358979323846f
#endif

#include "SimplePID/SimplePID.cpp"
#include "peripherals/HeatBalancePredictor.cpp"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Predictor unit tests
// ---------------------------------------------------------------------------

void test_disabled_when_any_parameter_missing() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.0f, 20.0f);
    TEST_ASSERT_FALSE(p.isEnabled());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.update(1.0f, 1.0f, 0.0f, 93.0f, 98.0f));
    p.configure(1100.0f, 0.6f, -1.0f);
    TEST_ASSERT_FALSE(p.isEnabled());
    p.configure(NAN, 0.6f, 20.0f);
    TEST_ASSERT_FALSE(p.isEnabled());
}

// Full power for longer than the dead time: the correction is the rise the sensor hasn't shown yet,
// i.e. rate × dead time.
void test_full_power_correction_is_rate_times_dead_time() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    for (int i = 0; i < 60; ++i)
        p.update(1.0f, 1.0f, 0.0f, 60.0f, 98.0f); // far from setpoint: no loss learning
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.6f * 20.0f, p.getCorrection());
}

// Heat in flight drains out of the window once the heater stops.
void test_correction_decays_after_heater_off() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    for (int i = 0; i < 30; ++i)
        p.update(1.0f, 1.0f, 0.0f, 60.0f, 98.0f);
    for (int i = 0; i < 20; ++i)
        p.update(1.0f, 0.0f, 0.0f, 60.0f, 98.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, p.getCorrection());
}

// A flush is seen at once: 10 ml/s of 23 °C water into a 98 °C boiler removes ~3.1 kW.
void test_flush_gives_immediate_negative_correction() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    for (int i = 0; i < 5; ++i)
        p.update(1.0f, 0.0f, 10.0f, 98.0f, 98.0f);
    const float capacity = 1100.0f / 0.6f;
    const float expected = -5.0f * 10.0f * 4.18f * (98.0f - 23.0f) / capacity;
    TEST_ASSERT_FLOAT_WITHIN(0.05f, expected, p.getCorrection());
}

// Holding temperature, the learned loss converges to the holding power, so the steady correction → 0 and the
// PID still settles the real sensor on setpoint.
void test_steady_state_correction_converges_to_zero() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    for (int i = 0; i < 3600; ++i)
        p.update(1.0f, 0.06f, 0.0f, 98.0f, 98.0f); // 66 W holding power
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 66.0f, p.getLossWatts());
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, p.getCorrection());
}

void test_correction_is_bounded() {
    HeatBalancePredictor p;
    p.configure(3000.0f, 5.0f, 120.0f); // 600 °C of "in flight" heat if unbounded
    for (int i = 0; i < 200; ++i)
        p.update(1.0f, 1.0f, 0.0f, 60.0f, 98.0f);
    TEST_ASSERT_EQUAL_FLOAT(HeatBalancePredictor::MAX_CORRECTION, p.getCorrection());
}

// Re-sending identical settings (the display does this after every reconnect) must not drop learned state.
void test_identical_reconfigure_keeps_state() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    for (int i = 0; i < 600; ++i)
        p.update(1.0f, 0.06f, 0.0f, 98.0f, 98.0f);
    const float loss = p.getLossWatts();
    p.configure(1100.0f, 0.6f, 20.0f);
    TEST_ASSERT_EQUAL_FLOAT(loss, p.getLossWatts());
    p.configure(1100.0f, 0.5f, 20.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.getLossWatts());
}

// ---------------------------------------------------------------------------
// Closed-loop flush replay
// ---------------------------------------------------------------------------
//
// Plant: two thermal nodes — the element and boiler wall (700 J/°C) feeding the water (1133 J/°C) through
// 35 W/°C — so 1100 W heats the whole at 0.6 °C/s and the wall keeps releasing stored heat after the heater
// stops, which is what makes the real overshoot. The thermocouple sees the water through a 6 s transport delay
// and a 4 s first-order lag (~20 s apparent dead time with the wall), then MAX31855 quantisation (0.25 °C) and
// the driver's 0.2 EMA at 4 Hz.
// Controller: SimplePID at 1 Hz on 0..1000 ms of a 1 s window, mirroring Heater::loopPid/updateLagCompensation.
// Scenario: settle at 98 °C (93 + 5 offset), then a 5 s, 10 ml/s flush of 20 °C water.

struct Tuning {
    float kp = 60.0f, ki = 0.658f, kd = 2800.0f;
    float derivFilterS = 0.0f;
    float integralZone = 0.0f;
    float modelW = 0.0f, modelRate = 0.0f, modelDeadS = 0.0f;
};

struct Result {
    float dipMin;    // lowest sensor temperature after the flush
    float peak;      // highest sensor temperature after the dip
    float settleErr; // |mean error| over the last 2 minutes
    float idleP2P;   // sensor peak-to-peak while idling before the flush
    float idleSwing; // mean |change in output| per second while idling (heater chatter), output units
};

static Result simulate(const Tuning &tu) {
    const float dt = 0.25f, setpoint = 98.0f, room = 22.0f, lossPerK = 0.85f; // ~65 W at 98 °C
    const float plantW = 1100.0f, wallC = 700.0f, waterC = 1133.0f, wallToWater = 35.0f;
    const float delayS = 6.0f, lagS = 4.0f;
    const int delaySteps = static_cast<int>(delayS / dt);

    float boiler = setpoint;
    float wall = setpoint + 65.0f / wallToWater; // carrying the holding power
    std::deque<float> pipe(delaySteps, boiler);
    float lagged = boiler, ema = boiler;

    float output = 0.0f, control = boiler, measured = boiler, sp = setpoint;
    SimplePID pid(&output, &control, &sp);
    pid.setDerivativeInput(&measured); // as Heater: D on the sensor, P/I on the prediction
    pid.setSamplingFrequency(1.0f);
    pid.setCtrlOutputLimits(0.0f, 1000.0f);
    pid.activateSetPointFilter(false);
    pid.setControllerPIDGains(tu.kp, tu.ki, tu.kd, 0.0f);
    pid.setDerivativeFilterTime(tu.derivFilterS);
    pid.setIntegralZone(tu.integralZone);
    pid.reset();
    pid.setMode(SimplePID::Control::automatic);

    HeatBalancePredictor predictor;
    predictor.configure(tu.modelW, tu.modelRate, tu.modelDeadS, 20.0f);
    const uint32_t holdTailMs = tu.modelDeadS > 0.0f ? static_cast<uint32_t>(tu.modelDeadS * 1000.0f) : 0;
    uint32_t lastFlowMs = 0;
    bool flowSeen = false;

    const float flushStart = 1800.0f, flushEnd = flushStart + 5.0f, end = flushStart + 600.0f;
    Result r{1e9f, -1e9f, 0.0f, 0.0f, 0.0f};
    float swingSum = 0.0f, lastOutput = 0.0f;
    int swingN = 0;
    float idleMin = 1e9f, idleMax = -1e9f, errSum = 0.0f;
    int errN = 0;
    bool dipped = false;
    fakeNow = 1000;

    for (int step = 0; step * dt < end; ++step) {
        const float t = step * dt;
        const float flow = (t >= flushStart && t < flushEnd) ? 10.0f : 0.0f;

        // Controller, once per second (the PWM window); `output` was applied over the last second.
        if (step % 4 == 0) {
            fakeNow += 1000;
            measured = ema; // read() returns the driver's EMA of quantised samples
            if (flow > 0.0f) {
                lastFlowMs = fakeNow;
                flowSeen = true;
            }
            if (tu.integralZone > 0.0f)
                pid.setIntegralHold(flow > 0.0f || (flowSeen && fakeNow - lastFlowMs < holdTailMs));
            predictor.update(1.0f, output / 1000.0f, flow, measured, sp);
            control = measured + predictor.getCorrection();
            pid.update();
            if (t > flushStart - 600.0f && t < flushStart) {
                swingSum += std::fabs(output - lastOutput);
                ++swingN;
            }
            lastOutput = output;

            if (t > flushStart - 600.0f && t < flushStart) {
                idleMin = std::min(idleMin, measured);
                idleMax = std::max(idleMax, measured);
            }
            if (t >= flushStart) {
                if (!dipped) {
                    r.dipMin = std::min(r.dipMin, measured);
                    if (measured > r.dipMin + 0.5f)
                        dipped = true;
                } else {
                    r.peak = std::max(r.peak, measured);
                }
            }
            if (t > end - 120.0f) {
                errSum += measured - setpoint;
                ++errN;
            }
        }

        // Plant
        const float heat = plantW * output / 1000.0f;
        const float toWater = wallToWater * (wall - boiler);
        const float water = flow * 4.18f * (boiler - 20.0f);
        wall += (heat - toWater) * dt / wallC;
        boiler += (toWater - water - lossPerK * (boiler - room)) * dt / waterC;
        pipe.push_back(boiler);
        const float delayed = pipe.front();
        pipe.pop_front();
        lagged += (delayed - lagged) * dt / lagS;
        const float sample = std::floor(lagged * 4.0f) / 4.0f; // MAX31855 LSB
        ema = 0.2f * sample + 0.8f * ema;
    }
    r.settleErr = std::fabs(errSum / static_cast<float>(errN));
    r.idleP2P = idleMax - idleMin;
    r.idleSwing = swingSum / static_cast<float>(swingN);
    return r;
}

static void report(const char *name, const Result &r) {
    printf("%-34s dip %6.2f  peak %6.2f  overshoot %5.2f  settle err %4.2f  idle p2p %4.2f  idle swing %5.1f\n", name, r.dipMin,
           r.peak, r.peak - 98.0f, r.settleErr, r.idleP2P, r.idleSwing);
}

static Tuning compensated() {
    Tuning tu;
    tu.derivFilterS = 2.0f;
    tu.integralZone = 1.5f;
    tu.modelW = 1100.0f;
    tu.modelRate = 0.6f;
    tu.modelDeadS = 20.0f;
    return tu;
}

// The legacy loop overshoots by ~3 °C after the flush whatever the gains; the full compensation removes most of it
// while still settling on setpoint and holding steady at idle.
void test_flush_overshoot_is_reduced() {
    const Result legacy = simulate(Tuning{});
    const Result comp = simulate(compensated());
    report("legacy 60/0.658/2800", legacy);
    report("compensated", comp);

    TEST_ASSERT_TRUE_MESSAGE(legacy.peak - 98.0f > 1.5f, "plant should reproduce a clear post-flush overshoot");
    TEST_ASSERT_TRUE(comp.peak - 98.0f < 0.5f * (legacy.peak - 98.0f));
    TEST_ASSERT_TRUE(comp.dipMin >= legacy.dipMin - 0.75f); // recovering without overshoot must not deepen the dip much
    TEST_ASSERT_TRUE(comp.settleErr < 0.3f);
    TEST_ASSERT_TRUE(comp.idleP2P <= 0.5f);
    TEST_ASSERT_TRUE(comp.idleSwing < 0.25f * legacy.idleSwing); // far less heater chatter at idle
}

// Each part on its own: the D filter cuts chatter without hurting the overshoot, the integral zone and the
// predictor each cut the overshoot.
void test_individual_features() {
    const Result legacy = simulate(Tuning{});
    Tuning filt;
    filt.derivFilterS = 2.0f;
    Tuning zone;
    zone.integralZone = 1.5f;
    zone.modelDeadS = 20.0f; // I pause tail only; predictor stays off without W/rate
    Tuning pred = compensated();
    pred.derivFilterS = 0.0f;
    pred.integralZone = 0.0f;
    const Result rf = simulate(filt), rz = simulate(zone), rp = simulate(pred);
    report("D filter only", rf);
    report("integral zone + pause only", rz);
    report("predictor only", rp);
    Tuning slow;
    slow.derivFilterS = 8.0f;
    report("D filter 8 s only", simulate(slow));

    TEST_ASSERT_TRUE(rf.peak <= legacy.peak + 0.25f);
    TEST_ASSERT_TRUE(rf.idleSwing < 0.25f * legacy.idleSwing);
    TEST_ASSERT_TRUE(rz.peak - 98.0f < 0.5f * (legacy.peak - 98.0f));
    TEST_ASSERT_TRUE(rp.peak - 98.0f < 0.6f * (legacy.peak - 98.0f));
    TEST_ASSERT_TRUE(rp.settleErr < 0.3f);
}

// The model is rarely exact: ±25 % on heat-up rate and dead time must stay stable and still beat the legacy loop.
void test_robust_to_model_mismatch() {
    const Result legacy = simulate(Tuning{});
    const float rates[] = {0.45f, 0.75f};
    const float deads[] = {15.0f, 25.0f};
    for (float rate : rates) {
        for (float dead : deads) {
            Tuning tu = compensated();
            tu.modelRate = rate;
            tu.modelDeadS = dead;
            const Result r = simulate(tu);
            char name[64];
            snprintf(name, sizeof(name), "mismatch rate %.2f dead %.0f", rate, dead);
            report(name, r);
            TEST_ASSERT_TRUE_MESSAGE(r.peak < legacy.peak, name);
            TEST_ASSERT_TRUE_MESSAGE(r.settleErr < 0.4f, name);
            TEST_ASSERT_TRUE_MESSAGE(r.idleP2P <= 0.75f, name);
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_when_any_parameter_missing);
    RUN_TEST(test_full_power_correction_is_rate_times_dead_time);
    RUN_TEST(test_correction_decays_after_heater_off);
    RUN_TEST(test_flush_gives_immediate_negative_correction);
    RUN_TEST(test_steady_state_correction_converges_to_zero);
    RUN_TEST(test_correction_is_bounded);
    RUN_TEST(test_identical_reconfigure_keeps_state);
    RUN_TEST(test_flush_overshoot_is_reduced);
    RUN_TEST(test_individual_features);
    RUN_TEST(test_robust_to_model_mismatch);
    return UNITY_END();
}
