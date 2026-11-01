#ifndef FLUSH_TANK_CHECK_H
#define FLUSH_TANK_CHECK_H

#include <algorithm>
#include <cmath>
#include <vector>

// What tank temperature a flush implies, to catch a refill the tank estimate missed (TankTemperature.h).
//
// After a flush the boiler's 3-minute response (sensor reading, heater duty and pump flow, once a second) is replayed
// through the plant fitted to a Silvia (scripts/fit_plant.py, test/test_heat_balance_predictor): element -> water over
// ~21 s, a group head that carries the standing loss and trades heat with the water over ~2.4 min, and a sensor behind
// a short delay and a first-order lag. The tank temperature whose replay best matches the reading is the flush's
// answer. One flush is noisy (21-35.5 °C on 2026-10-29 with 32 °C measured) and reads ~4 °C low (the group head's
// extra cooling looks like colder water), so the caller adds BIAS_C and only acts when several flushes agree.
namespace flush_tank_check {

constexpr float BIAS_C = 4.0f;       // added to the fitted value (weighed flushes: 27.8 °C fitted vs 32 measured)
constexpr float RECORD_S = 180.0f;   // seconds of response after the flush starts
constexpr float SEARCH_MIN_C = 5.0f; // candidate tank temperatures
constexpr float SEARCH_MAX_C = 65.0f;
constexpr float SEARCH_STEP_C = 0.5f;
constexpr float MAX_RMS_C = 1.0f;     // worse fits (a shot, a disturbance) are ignored
constexpr float REFILL_DROP_C = 8.0f; // this much colder than the estimate ...
constexpr int REFILL_FLUSHES = 2;     // ... on this many flushes in a row counts as a refill

struct Sample {
    float temp; // sensor reading, °C (as shown; the offset cancels)
    float duty; // heater duty 0..1 over the last second
    float flow; // pump flow ml/s
};

struct Plant {
    float capacity = 1833.0f; // J/°C: heater power / full-power heat-up rate
    float heaterW = 1100.0f;
    float holdW = 45.0f;      // standing loss at the starting temperature
    float waterDeadS = 12.0f; // the sensor's delays scale with it (Silvia: 4.6 s + 15 s at 12 s)
};

// RMS error (°C) between the readings and a replay of the plant with this tank temperature.
inline float replayError(const std::vector<Sample> &s, const Plant &p, float tankC) {
    if (s.size() < 2)
        return INFINITY;
    const float elementC = 0.109f * p.capacity, waterC = 0.707f * p.capacity, groupC = 0.447f * p.capacity;
    const float toWaterPerC = elementC / 20.9f, toGroupPerC = groupC / 143.6f;
    const float delayS = 4.6f / 12.0f * p.waterDeadS, lagS = std::max(15.0f / 12.0f * p.waterDeadS, 1.0f);
    const float dt = 0.25f;
    const float start = s.front().temp;
    float water = start, element = start + p.holdW / toWaterPerC, group = start - p.holdW / toGroupPerC;
    float lag = start;
    const float lossPerC = p.holdW / std::max(start - 22.0f, 1.0f);
    const int delaySteps = std::max(1, static_cast<int>(std::lround(delayS / dt)));
    std::vector<float> pipe(static_cast<size_t>(delaySteps), start);
    size_t head = 0;
    float sum = 0.0f;
    for (size_t i = 0; i < s.size(); ++i) {
        const float diff = lag - s[i].temp;
        sum += diff * diff;
        for (int k = 0; k < 4; ++k) {
            const float toWater = toWaterPerC * (element - water);
            const float toGroup = toGroupPerC * (water - group);
            const float cold = std::max(s[i].flow, 0.0f) * 4.18f * (water - tankC);
            element += (p.heaterW * s[i].duty - toWater) * dt / elementC;
            group += (toGroup - lossPerC * (water - 22.0f)) * dt / groupC;
            water += (toWater - cold - toGroup) * dt / waterC;
            const float delayed = pipe[head];
            pipe[head] = water;
            head = (head + 1) % pipe.size();
            lag += (delayed - lag) * dt / lagS;
        }
    }
    return std::sqrt(sum / static_cast<float>(s.size()));
}

// Best-fitting tank temperature (°C) for one flush's response, or NAN if none fits well enough.
inline float impliedTank(const std::vector<Sample> &s, const Plant &p, float *rmsOut = nullptr) {
    float best = NAN, bestErr = INFINITY;
    for (float c = SEARCH_MIN_C; c <= SEARCH_MAX_C + 1e-3f; c += SEARCH_STEP_C) {
        const float e = replayError(s, p, c);
        if (e < bestErr) {
            bestErr = e;
            best = c;
        }
    }
    if (rmsOut)
        *rmsOut = bestErr;
    return bestErr <= MAX_RMS_C ? best + BIAS_C : NAN;
}

// Counts flushes that imply a much colder tank than the estimate; true when they say it was refilled.
class RefillDetector {
  public:
    bool onFlush(float implied, float estimate) {
        if (!std::isfinite(implied) || !std::isfinite(estimate))
            return false; // no verdict: keep the count
        if (implied < estimate - REFILL_DROP_C) {
            if (++coldFlushes >= REFILL_FLUSHES) {
                coldFlushes = 0;
                return true;
            }
        } else {
            coldFlushes = 0;
        }
        return false;
    }

  private:
    int coldFlushes = 0;
};

} // namespace flush_tank_check

#endif // FLUSH_TANK_CHECK_H
