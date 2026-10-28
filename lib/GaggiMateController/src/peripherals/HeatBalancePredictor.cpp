#include "HeatBalancePredictor.h"
#include <algorithm>
#include <cmath>
#include <numeric>

void HeatBalancePredictor::configure(float heaterWatts, float fullPowerRate, float deadTimeSeconds, float inletTemp,
                                     float waterDeadTimeSeconds) {
    const bool valid = std::isfinite(heaterWatts) && std::isfinite(fullPowerRate) && std::isfinite(deadTimeSeconds) &&
                       heaterWatts > 0.0f && fullPowerRate > 0.0f && deadTimeSeconds > 0.0f;
    const float capacity = valid ? heaterWatts / fullPowerRate : 0.0f;
    const float delay = valid ? deadTimeSeconds : 0.0f;
    const float waterDelay =
        valid ? (std::isfinite(waterDeadTimeSeconds) && waterDeadTimeSeconds > 0.0f ? waterDeadTimeSeconds : delay) : 0.0f;
    if (capacity == heatCapacity && delay == deadTime && waterDelay == waterDeadTime && heaterWatts == this->heaterWatts &&
        inletTemp == this->inletTemp)
        return; // unchanged re-send: keep the in-flight window
    this->heaterWatts = valid ? heaterWatts : 0.0f;
    heatCapacity = capacity;
    deadTime = delay;
    waterDeadTime = waterDelay;
    this->inletTemp = inletTemp;
    // The standing loss is a property of the boiler, not of these parameters, so it survives a reconfigure.
    reset();
}

void HeatBalancePredictor::seedLoss(float savedWatts) {
    if (!isEnabled() || lossSeeded)
        return;
    lossWatts =
        std::isfinite(savedWatts) && savedWatts > 0.0f ? std::min(savedWatts, heaterWatts) : DEFAULT_LOSS_FRACTION * heaterWatts;
    lossSeeded = true;
}

void HeatBalancePredictor::reset() {
    window.clear();
    windowTime = 0.0f;
    correction = 0.0f;
}

float HeatBalancePredictor::update(float dt, float duty, float flow, float measuredTemp, float setpoint) {
    if (!isEnabled() || !(dt > 0.0f) || !std::isfinite(measuredTemp)) {
        correction = 0.0f;
        return correction;
    }
    duty = std::min(std::max(duty, 0.0f), 1.0f);
    flow = std::isfinite(flow) && flow > 0.0f ? flow : 0.0f;

    const float heaterPower = heaterWatts * duty;
    const float waterPower = flow * WATER_SPECIFIC_HEAT * std::max(measuredTemp - inletTemp, 0.0f);

    // Standing losses: at rest near setpoint the heater power that holds temperature is the loss. Learning it
    // keeps the steady-state correction at zero, so the PID still settles the real sensor on setpoint.
    if (flow == 0.0f && setpoint > 0.0f && std::fabs(measuredTemp - setpoint) < LOSS_LEARN_BAND) {
        lossWatts += (heaterPower - lossWatts) * dt / (LOSS_LEARN_TAU + dt);
    }

    window.push_back({dt, (heaterPower - lossWatts) * dt / heatCapacity, waterPower * dt / heatCapacity});
    windowTime += dt;
    // Steps older than both delays have reached the sensor; they are in measuredTemp already.
    const float longest = std::max(deadTime, waterDeadTime);
    while (!window.empty() && windowTime - window.front().dt >= longest) {
        windowTime -= window.front().dt;
        window.pop_front();
    }
    // Sum, newest first, what each path hasn't delivered to the sensor yet (re-summed each step, so float drift can't
    // build up over hours). A step counts while the time since it is still shorter than that path's delay.
    float pending = 0.0f;
    float since = 0.0f;
    for (auto it = window.rbegin(); it != window.rend(); ++it) {
        if (since < deadTime)
            pending += it->heat;
        if (since < waterDeadTime)
            pending -= it->water;
        since += it->dt;
    }

    correction = std::min(std::max(pending, -MAX_CORRECTION), MAX_CORRECTION);
    return correction;
}
