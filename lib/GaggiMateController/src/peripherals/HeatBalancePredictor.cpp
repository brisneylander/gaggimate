#include "HeatBalancePredictor.h"
#include <algorithm>
#include <cmath>
#include <numeric>

void HeatBalancePredictor::configure(float heaterWatts, float fullPowerRate, float deadTimeSeconds, float inletTemp) {
    const bool valid = std::isfinite(heaterWatts) && std::isfinite(fullPowerRate) && std::isfinite(deadTimeSeconds) &&
                       heaterWatts > 0.0f && fullPowerRate > 0.0f && deadTimeSeconds > 0.0f;
    const float capacity = valid ? heaterWatts / fullPowerRate : 0.0f;
    const float delay = valid ? deadTimeSeconds : 0.0f;
    if (capacity == heatCapacity && delay == deadTime && heaterWatts == this->heaterWatts && inletTemp == this->inletTemp)
        return; // unchanged re-send: keep the in-flight window
    this->heaterWatts = valid ? heaterWatts : 0.0f;
    heatCapacity = capacity;
    deadTime = delay;
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
    windowRise = 0.0f;
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

    const float rise = (heaterPower - waterPower - lossWatts) * dt / heatCapacity;
    window.push_back({dt, rise});
    windowTime += dt;
    // Steps older than the dead time have reached the sensor; they are in measuredTemp already.
    while (!window.empty() && windowTime - window.front().dt >= deadTime) {
        windowTime -= window.front().dt;
        window.pop_front();
    }
    // Re-summed (~dead-time samples) so float drift can't build up over hours.
    windowRise = std::accumulate(window.begin(), window.end(), 0.0f, [](float sum, const Step &step) { return sum + step.rise; });

    correction = std::min(std::max(windowRise, -MAX_CORRECTION), MAX_CORRECTION);
    return correction;
}
