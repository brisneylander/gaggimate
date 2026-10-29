#include "ThermalObserver.h"
#include <algorithm>
#include <cmath>

void ThermalObserver::configure(float heaterWatts, float fullPowerRate, float waterDeadTimeS, float inletTemp) {
    const bool valid = std::isfinite(heaterWatts) && std::isfinite(fullPowerRate) && std::isfinite(waterDeadTimeS) &&
                       heaterWatts > 0.0f && fullPowerRate > 0.0f && waterDeadTimeS > 0.0f;
    const float newCapacity = valid ? heaterWatts / fullPowerRate : 0.0f;
    const float newDelay = valid ? SENSOR_DELAY_PER_WATER_DEAD * waterDeadTimeS : 0.0f;
    const float newLag = valid ? std::max(SENSOR_LAG_PER_WATER_DEAD * waterDeadTimeS, 1.0f) : 1.0f;
    const float newInlet = std::isfinite(inletTemp) && inletTemp > 0.0f ? inletTemp : 23.0f;
    if (newCapacity == capacity && heaterWatts == heaterW && newDelay == delayS && newLag == lagS && newInlet == this->inletTemp)
        return; // unchanged re-send: keep the estimate
    capacity = newCapacity;
    heaterW = valid ? heaterWatts : 0.0f;
    delayS = newDelay;
    lagS = newLag;
    this->inletTemp = newInlet;
    primed = false; // restart from the next reading
}

void ThermalObserver::reset(float temp, float lossWatts) {
    const float loss = std::isfinite(lossWatts) && lossWatts > 0.0f ? lossWatts : 0.05f * heaterW;
    const float elementC = ELEMENT_SHARE * capacity, groupC = GROUP_SHARE * capacity;
    const float toWaterPerC = elementC / ELEMENT_TAU_S, toGroupPerC = groupC / GROUP_TAU_S;
    lossPerC = temp > ROOM_C + 1.0f ? loss / (temp - ROOM_C) : 0.0f;
    water = temp;
    element = temp + loss / toWaterPerC; // carrying the holding power
    group = temp - loss / toGroupPerC;   // the standing loss leaves through the group
    sensor = temp;
    biasW = 0.0f;
    pipe.clear();
    pipeTime = 0.0f;
    primed = true;
}

float ThermalObserver::update(float dt, float duty, float flow, float extraWaterMl, float measuredTemp) {
    if (!isEnabled() || !(dt > 0.0f) || !std::isfinite(measuredTemp))
        return measuredTemp;
    if (!primed)
        reset(measuredTemp, 0.0f);
    duty = std::min(std::max(duty, 0.0f), 1.0f);
    flow = std::isfinite(flow) && flow > 0.0f ? flow : 0.0f;
    extraWaterMl = std::isfinite(extraWaterMl) && extraWaterMl > 0.0f ? extraWaterMl : 0.0f;

    const float elementC = ELEMENT_SHARE * capacity, waterC = WATER_SHARE * capacity, groupC = GROUP_SHARE * capacity;
    const float toWater = elementC / ELEMENT_TAU_S * (element - water);
    const float toGroup = groupC / GROUP_TAU_S * (water - group);
    const float coldJ = (flow * dt + extraWaterMl) * WATER_SPECIFIC_HEAT * std::max(water - inletTemp, 0.0f);
    element += (heaterW * duty - toWater) * dt / elementC;
    group += (toGroup - lossPerC * (water - ROOM_C)) * dt / groupC;
    water += ((toWater - toGroup + biasW) * dt - coldJ) / waterC;

    // The sensor sees the water through a transport delay, then a first-order lag.
    pipe.emplace_back(dt, water);
    pipeTime += dt;
    float delayed = pipe.front().second;
    while (pipe.size() > 1 && pipeTime - pipe.front().first >= delayS) {
        pipeTime -= pipe.front().first;
        pipe.pop_front();
        delayed = pipe.front().second;
    }
    sensor += (delayed - sensor) * std::min(dt / lagS, 1.0f);

    // Correct towards the reading: shift the whole chain (an offset the sensor shows straight away) and learn a slow
    // heat bias so a persistent mismatch stops recurring.
    const float innovation = measuredTemp - sensor;
    const float shift = std::min(gainWater * dt, 1.0f) * innovation;
    water += shift;
    element += shift + gainElement * dt * innovation;
    group += shift;
    sensor += shift;
    for (auto &p : pipe)
        p.second += shift;
    biasW = std::min(std::max(biasW + gainBiasW * dt * innovation, -0.5f * heaterW), 0.5f * heaterW);
    return water;
}

float ThermalObserver::getSettling() const {
    if (!isEnabled())
        return water;
    const float elementC = ELEMENT_SHARE * capacity, waterC = WATER_SHARE * capacity;
    // At rest the element leads the water by holding power / conductance: that heat flows on to the losses, not into the
    // water's temperature, so it isn't counted.
    const float holdingLead = lossPerC * (water - ROOM_C) * ELEMENT_TAU_S / elementC;
    const float lead = element - water - holdingLead;
    const float groupC = GROUP_SHARE * capacity;
    const float groupRest = water - lossPerC * (water - ROOM_C) * GROUP_TAU_S / groupC; // resting group temperature
    const float groupDeficit = std::min(group - groupRest, 0.0f) * groupWeight;
    const float weightG = groupWeight * groupC;
    return water + (lead * elementC + groupDeficit * groupC) / (elementC + waterC + weightG);
}
