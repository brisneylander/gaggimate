#ifndef HEATUPBOOST_H
#define HEATUPBOOST_H

#include <cstdint>

// Full power while the boiler is well below the setpoint: a warm-up, a switch to steam, steaming milk.
//
// With the heat-balance predictor on, the PID runs on measured + heat in flight. At full power that is ~12 °C on a Silvia
// (20 s x 1100 W / 1833 J/°C), and Kd brakes on the rise, so the heater fell to 10-20 % while the boiler was still
// 10-30 °C under (bench, 2026-10-30 morning: 6-7 min to warm up from 71 °C, steam heat-up held to ~20 %, steaming at
// ~70 %). Below BELOW_C of predicted shortfall nothing is close enough to overshoot from, so run flat out and let the
// PID take over near the setpoint. Not within AFTER_FLOW_MS of water flowing: after a flush the reading dips and
// rebounds by itself, and the predictor and PID already handle that (simulator: 5 °C reaches the setpoint in 80-170 s
// instead of 355-590 s; 2-3 °C overshoots by 2.5-4.75 °C).
struct HeatUpBoost {
    static constexpr float BELOW_C = 5.0f;
    static constexpr uint32_t AFTER_FLOW_MS = 120000;

    static bool active(bool predictorOn, float setpoint, float controlTemperature, bool flowSeen, uint32_t msSinceFlow) {
        if (!predictorOn || setpoint <= 0.0f)
            return false;
        if (flowSeen && msSinceFlow < AFTER_FLOW_MS)
            return false;
        return setpoint - controlTemperature > BELOW_C;
    }
};

#endif // HEATUPBOOST_H
