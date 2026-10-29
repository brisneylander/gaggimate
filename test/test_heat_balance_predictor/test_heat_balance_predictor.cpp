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

// With its own, shorter delay a flush stops counting once the sensor has seen it (5 s), instead of after the heater's
// dead time (20 s), so it isn't counted twice.
void test_water_delay_drains_flush_sooner() {
    HeatBalancePredictor shared, separate;
    shared.configure(1100.0f, 0.6f, 20.0f, 23.0f);
    separate.configure(1100.0f, 0.6f, 20.0f, 23.0f, 5.0f);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, shared.getWaterDeadTime());
    TEST_ASSERT_EQUAL_FLOAT(5.0f, separate.getWaterDeadTime());
    for (int i = 0; i < 5; ++i) { // 5 s flush, heater off
        shared.update(1.0f, 0.0f, 10.0f, 98.0f, 98.0f);
        separate.update(1.0f, 0.0f, 10.0f, 98.0f, 98.0f);
    }
    for (int i = 0; i < 10; ++i) {
        shared.update(1.0f, 0.0f, 0.0f, 90.0f, 98.0f);
        separate.update(1.0f, 0.0f, 0.0f, 90.0f, 98.0f);
    }
    TEST_ASSERT_TRUE(shared.getCorrection() < -3.0f); // still counting the flush 10 s after it ended
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, separate.getCorrection());
}

// Heater heat keeps the full dead time even when water has a shorter one.
void test_water_delay_leaves_heater_delay_alone() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f, 23.0f, 5.0f);
    for (int i = 0; i < 60; ++i)
        p.update(1.0f, 1.0f, 0.0f, 60.0f, 98.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.6f * 20.0f, p.getCorrection());
}

// Warmer tank water takes proportionally less heat per ml: a 40 °C tank (measured on a Silvia) removes
// (98 - 40) / (98 - 23) = 77 % of what the old fixed 23 °C assumption did.
void test_flush_correction_scales_with_inlet_temperature() {
    HeatBalancePredictor cold, warm;
    cold.configure(1100.0f, 0.6f, 20.0f, 23.0f);
    warm.configure(1100.0f, 0.6f, 20.0f, 40.0f);
    for (int i = 0; i < 5; ++i) {
        cold.update(1.0f, 0.0f, 10.0f, 98.0f, 98.0f);
        warm.update(1.0f, 0.0f, 10.0f, 98.0f, 98.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (98.0f - 40.0f) / (98.0f - 23.0f), warm.getCorrection() / cold.getCorrection());
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

// Re-sending settings (the display does this after every reconnect) must not drop the learned loss, and neither
// may a parameter change: the loss belongs to the boiler, not to the model parameters.
void test_identical_reconfigure_keeps_state() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    for (int i = 0; i < 600; ++i)
        p.update(1.0f, 0.06f, 0.0f, 98.0f, 98.0f);
    const float loss = p.getLossWatts();
    p.configure(1100.0f, 0.6f, 20.0f);
    TEST_ASSERT_EQUAL_FLOAT(loss, p.getLossWatts());
    p.configure(1100.0f, 0.5f, 20.0f);
    TEST_ASSERT_EQUAL_FLOAT(loss, p.getLossWatts());
}

// With nothing saved, the predictor starts from a default fraction of the heater instead of zero.
void test_first_seed_uses_default_fraction() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    p.seedLoss(0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, HeatBalancePredictor::DEFAULT_LOSS_FRACTION * 1100.0f, p.getLossWatts());
}

// A saved value seeds the loss once; later seeds (every settings re-send carries the saved value) are ignored so
// they can't overwrite what has been learned since.
void test_saved_seed_applies_once() {
    HeatBalancePredictor p;
    p.configure(1100.0f, 0.6f, 20.0f);
    p.seedLoss(48.0f);
    TEST_ASSERT_EQUAL_FLOAT(48.0f, p.getLossWatts());
    for (int i = 0; i < 600; ++i)
        p.update(1.0f, 0.06f, 0.0f, 98.0f, 98.0f); // learns towards 66 W
    const float learned = p.getLossWatts();
    TEST_ASSERT_TRUE(learned > 55.0f);
    p.seedLoss(48.0f);
    TEST_ASSERT_EQUAL_FLOAT(learned, p.getLossWatts());
}

// Seeding waits for a valid model, and a saved value can't exceed the heater itself.
void test_seed_waits_for_enable_and_is_bounded() {
    HeatBalancePredictor p;
    p.seedLoss(48.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.getLossWatts());
    p.configure(1100.0f, 0.6f, 20.0f);
    p.seedLoss(5000.0f);
    TEST_ASSERT_EQUAL_FLOAT(1100.0f, p.getLossWatts());
}

// ---------------------------------------------------------------------------
// Closed-loop flush replay
// ---------------------------------------------------------------------------
//
// Plant, fitted to a Rancilio Silvia (scripts/fit_plant.py over the 2026-10-29 bench logs, replaying the logged heater
// output and pump flow open loop): three thermal nodes. The element (200 J/°C) feeds the water (1296 J/°C) through
// 9.6 W/°C, so heater heat reaches the water with a ~21 s time constant and keeps arriving after the heater stops,
// which makes the real overshoot. The group head (819 J/°C) trades heat with the water through 5.7 W/°C (~2.4 min) and
// carries the standing loss (~45 W at 98 °C), so it sits ~8 °C below the water: after a flush or a fast reheat it keeps
// pulling the water down, or soaking up the extra, for minutes. The thermocouple sees the water through a 4.6 s
// transport delay and a 15 s first-order lag (flush water shows after ~12 s, heater heat after ~40 s), then MAX31855
// quantisation (0.25 °C) and the driver's 0.2 EMA at 4 Hz. Against bench flushes it doesn't see in fitting, it tracks
// the sensor to 0.45 °C rms (the earlier two-node plant: 1.09 °C) and puts the dip's low within ~6 s.
// Controller: SimplePID at 1 Hz on 0..1000 ms of a 1 s window, mirroring Heater::loopPid/updateLagCompensation.
// Scenario: settle at 98 °C (93 + 5 offset), then a 5 s, 8.9 ml/s flush of 35 °C tank water (as on the bench).

struct Tuning {
    float kp = 60.0f, ki = 0.658f, kd = 2800.0f;
    float derivFilterS = 0.0f;
    float integralZone = 0.0f;
    float modelW = 0.0f, modelRate = 0.0f, modelDeadS = 0.0f;
    float lossSeedW = 0.0f;       // as Heater: seedLoss(saved value, 0 = default guess); < 0 skips seeding (starts at 0)
    float waterDeadS = 0.0f;      // predictor's water delay, 0 = same as modelDeadS
    bool reheat = false;          // as Heater: full power while the predictor says energy is owed
    float predictorStartS = 0.0f; // switch the predictor on this long into the run, once the PID has settled
    // Tank temperature the flush water really cools like (plant) and the one the predictor is told. On the bench each ml
    // took ~20 % more heat than (boiler - tank setting) predicts: fitted sessions cool like 24-27 °C water at 35 °C.
    float plantInletC = 35.0f, predictorInletC = 35.0f;
};

struct Result {
    float dipMin;    // lowest sensor temperature after the flush
    float peak;      // highest sensor temperature after the dip
    float settleErr; // |mean error| over the last 2 minutes
    float idleP2P;   // sensor peak-to-peak while idling before the flush
    float idleSwing; // mean |change in output| per second while idling (heater chatter), output units
    float startSag;  // lowest (sensor - setpoint) in the 10 minutes after the predictor starts (predictorStartS > 0)
    float backAt;    // s after the flush starts until the sensor first climbs back to setpoint
};

static Result simulate(const Tuning &tu) {
    const float dt = 0.25f, setpoint = 98.0f, room = 22.0f, lossPerK = 0.59f; // ~45 W at 98 °C
    const float plantW = 1100.0f, wallC = 200.0f, waterC = 1296.0f, wallToWater = 9.6f;
    const float groupC = 819.0f, waterToGroup = 5.7f;
    const float inletC = tu.plantInletC, flushFlow = 8.9f;
    const float delayS = 4.6f, lagS = 15.0f;
    const int delaySteps = static_cast<int>(delayS / dt);

    const float holdW = lossPerK * (setpoint - room);
    float boiler = setpoint;
    float wall = setpoint + holdW / wallToWater;   // carrying the holding power
    float group = setpoint - holdW / waterToGroup; // the standing loss leaves through the group
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
    bool predictorStarted = false;
    const auto startPredictor = [&] {
        predictor.configure(tu.modelW, tu.modelRate, tu.modelDeadS, tu.predictorInletC, tu.waterDeadS);
        if (tu.lossSeedW >= 0.0f)
            predictor.seedLoss(tu.lossSeedW);
        predictorStarted = true;
    };
    if (tu.predictorStartS <= 0.0f)
        startPredictor();
    const uint32_t holdTailMs = tu.modelDeadS > 0.0f ? static_cast<uint32_t>(tu.modelDeadS * 1000.0f) : 0;
    uint32_t lastFlowMs = 0;
    bool flowSeen = false;

    const float flushStart = 1800.0f, flushEnd = flushStart + 5.0f, end = flushStart + 600.0f;
    Result r{1e9f, -1e9f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f};
    float swingSum = 0.0f, lastOutput = 0.0f;
    int swingN = 0;
    float idleMin = 1e9f, idleMax = -1e9f, errSum = 0.0f;
    int errN = 0;
    bool dipped = false;
    fakeNow = 1000;

    for (int step = 0; step * dt < end; ++step) {
        const float t = step * dt;
        const float flow = (t >= flushStart && t < flushEnd) ? flushFlow : 0.0f;

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
            if (!predictorStarted && t >= tu.predictorStartS)
                startPredictor();
            if (predictorStarted)
                predictor.update(1.0f, output / 1000.0f, flow, measured, sp);
            control = measured + predictor.getCorrection();
            pid.update();
            if (tu.reheat && predictor.eventDuty() >= 0.0f)
                output = predictor.eventDuty() * 1000.0f;
            if (t > flushStart - 600.0f && t < flushStart) {
                swingSum += std::fabs(output - lastOutput);
                ++swingN;
            }
            lastOutput = output;

            if (tu.predictorStartS > 0.0f && t >= tu.predictorStartS && t < tu.predictorStartS + 600.0f)
                r.startSag = std::min(r.startSag, measured - setpoint);
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
                    if (r.backAt < 0.0f && measured >= setpoint)
                        r.backAt = t - flushStart;
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
        const float toGroup = waterToGroup * (boiler - group);
        const float water = flow * 4.18f * (boiler - inletC);
        wall += (heat - toWater) * dt / wallC;
        group += (toGroup - lossPerK * (boiler - room)) * dt / groupC;
        boiler += (toWater - water - toGroup) * dt / waterC;
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
    printf("%-34s dip %6.2f  peak %6.2f  overshoot %5.2f  back %5.0f s  settle err %4.2f  idle p2p %4.2f  idle swing %5.1f\n",
           name, r.dipMin, r.peak, r.peak - 98.0f, r.backAt, r.settleErr, r.idleP2P, r.idleSwing);
}

// The Silvia's settings (simon006 on): Kd 1500, D filter, integral zone, predictor with a 12 s water delay.
static Tuning compensated() {
    Tuning tu;
    tu.kd = 1500.0f;
    tu.waterDeadS = 12.0f;
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

// Each part on its own: the D filter cuts chatter, the predictor cuts the overshoot. On the fitted plant a 2 s filter
// alone delays the D term's braking enough to cost ~0.75 °C more overshoot (8 s: ~2.75 °C), which is why it ships with
// the predictor rather than on its own.
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

    TEST_ASSERT_TRUE(rf.peak <= legacy.peak + 1.0f);
    TEST_ASSERT_TRUE(rf.idleSwing < 0.5f * legacy.idleSwing); // ~3x less chatter (2.5 vs 7.4)
    // On the fitted plant most of the overshoot is element heat still arriving, which the integral can't see coming:
    // the zone alone trims it a little (3.0 -> 2.75 °C) but mustn't make it worse.
    TEST_ASSERT_TRUE(rz.peak <= legacy.peak);
    // 75 %: the sensor's 0.25 degC steps make the simulated peak move in quarter-degree jumps.
    TEST_ASSERT_TRUE(rp.peak - 98.0f < 0.75f * (legacy.peak - 98.0f));
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

// Switching the predictor on while the PID holds setpoint, as on the bench: from zero loss it reads the ~65 W holding
// power as heat in flight and sags the boiler by about dead time x holding power / capacity (0.49 degC measured on a
// Silvia) until the loss is learned. The default guess shrinks that and a saved value removes it.
// Full-power reheat puts back the flush's heat as fast as the element allows. The earlier two-node plant promised it
// back on setpoint ~3x sooner; on the Silvia it was no faster (124-129 s vs 62-100 s), with the heat needing ~2 min to
// reach the thermocouple. The fitted plant gets back sooner only when the predictor's tank temperature is exact (it
// was ~10 °C warm on the bench, see test_bench_configurations), and its "back" time is sensitive to the 0.25 °C sensor
// steps, so this only checks that reheat stays bounded and settles; it is opt-in.
void test_full_power_reheat() {
    Tuning base = compensated();
    base.waterDeadS = 8.0f;
    Tuning fast = base;
    fast.reheat = true;
    const Result rb = simulate(base), rf = simulate(fast);
    report("water 8 s", rb);
    report("water 8 s + full-power reheat", rf);
    TEST_ASSERT_TRUE(rf.backAt > 0.0f && rb.backAt > 0.0f);
    TEST_ASSERT_TRUE(rf.peak - 98.0f <= rb.peak - 98.0f + 1.0f); // at most four sensor steps of extra overshoot
    TEST_ASSERT_TRUE(rf.settleErr < 0.3f);
    TEST_ASSERT_TRUE(rf.idleSwing <= rb.idleSwing + 0.5f); // reheat only runs during water events, not at idle
}

// Reheat with model errors: it must stay bounded (no runaway heat) and still settle.
void test_full_power_reheat_with_model_mismatch() {
    const float rates[] = {0.45f, 0.75f};
    for (float rate : rates) {
        Tuning tu = compensated();
        tu.waterDeadS = 8.0f;
        tu.reheat = true;
        tu.modelRate = rate;
        const Result r = simulate(tu);
        char name[48];
        snprintf(name, sizeof(name), "reheat, rate %.2f", rate);
        report(name, r);
        TEST_ASSERT_TRUE_MESSAGE(r.peak - 98.0f < 2.0f, name);
        TEST_ASSERT_TRUE_MESSAGE(r.settleErr < 0.4f, name);
    }
}

void test_separate_water_delay() {
    const Result shared = simulate(compensated());
    report("water delay = dead time (20 s)", shared);
    const float delays[] = {5.0f, 8.0f, 12.0f};
    for (float d : delays) {
        Tuning tu = compensated();
        tu.waterDeadS = d;
        const Result r = simulate(tu);
        char name[48];
        snprintf(name, sizeof(name), "water delay %.0f s", d);
        report(name, r);
        TEST_ASSERT_TRUE_MESSAGE(r.settleErr < 0.3f, name);
        TEST_ASSERT_TRUE_MESSAGE(r.idleP2P <= 0.5f, name);
    }
}

void test_seeded_start_avoids_sag() {
    Tuning zero = compensated();
    zero.lossSeedW = -1.0f;
    zero.predictorStartS = 600.0f;
    Tuning guess = compensated(); // default: 5 % of 1100 W = 55 W
    guess.predictorStartS = 600.0f;
    Tuning saved = compensated();
    saved.lossSeedW = 65.0f;
    saved.predictorStartS = 600.0f;
    const Result rz = simulate(zero), rg = simulate(guess), rs = simulate(saved);
    printf("start sag: from zero %.2f, default guess %.2f, saved value %.2f degC\n", rz.startSag, rg.startSag, rs.startSag);
    TEST_ASSERT_TRUE(rz.startSag < -0.4f);
    TEST_ASSERT_TRUE(rg.startSag > 0.5f * rz.startSag);
    TEST_ASSERT_TRUE(rs.startSag > -0.3f); // within one 0.25 degC sensor step, i.e. idle noise
}

// The bench configurations (Kp 60 / Ki 0.658 / Kd 1500, 5 s flush from 93 °C): the plant should rank them as the
// Silvia did. Bench overshoot: off +2.91, shared 20 s delay +1.68, 12 s water delay +0.63 °C; full-power reheat no
// faster back to 93 °C (124-129 s vs 62-100 s).
void test_bench_configurations() {
    Tuning off;
    off.kd = 1500.0f;
    off.plantInletC = 25.0f;
    Tuning shared = compensated();
    shared.waterDeadS = 0.0f;
    shared.plantInletC = 25.0f;
    Tuning water = compensated();
    water.plantInletC = 25.0f;
    Tuning reheat = water;
    reheat.reheat = true;
    const Result ro = simulate(off), rs = simulate(shared), rw = simulate(water), rr = simulate(reheat);
    report("bench: off", ro);
    report("bench: shared 20 s delay", rs);
    report("bench: 12 s water delay", rw);
    report("bench: 12 s water + reheat", rr);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_when_any_parameter_missing);
    RUN_TEST(test_full_power_correction_is_rate_times_dead_time);
    RUN_TEST(test_correction_decays_after_heater_off);
    RUN_TEST(test_flush_gives_immediate_negative_correction);
    RUN_TEST(test_flush_correction_scales_with_inlet_temperature);
    RUN_TEST(test_water_delay_drains_flush_sooner);
    RUN_TEST(test_water_delay_leaves_heater_delay_alone);
    RUN_TEST(test_steady_state_correction_converges_to_zero);
    RUN_TEST(test_correction_is_bounded);
    RUN_TEST(test_identical_reconfigure_keeps_state);
    RUN_TEST(test_first_seed_uses_default_fraction);
    RUN_TEST(test_saved_seed_applies_once);
    RUN_TEST(test_seed_waits_for_enable_and_is_bounded);
    RUN_TEST(test_flush_overshoot_is_reduced);
    RUN_TEST(test_individual_features);
    RUN_TEST(test_robust_to_model_mismatch);
    RUN_TEST(test_seeded_start_avoids_sag);
    RUN_TEST(test_separate_water_delay);
    RUN_TEST(test_full_power_reheat);
    RUN_TEST(test_full_power_reheat_with_model_mismatch);
    RUN_TEST(test_bench_configurations);
    return UNITY_END();
}
