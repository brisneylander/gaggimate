#ifndef FLUSH_TO_TARGET_H
#define FLUSH_TO_TARGET_H

#include <algorithm>
#include <cmath>

// "Flush to target": when the boiler is above its target (after steaming, or an overshoot), flush just enough fresh
// tank water to bring it back. Every ml drawn in replaces boiler water and takes 4.18 J/°C x (boiler - tank); the
// boiler holds heat capacity = heater power / full-power heat-up rate. User-started only: the flush runs through the
// group, so it must not fire with a portafilter of coffee locked in.
namespace flush_to_target {

constexpr float WATER_SPECIFIC_HEAT = 4.18f; // J/(g·°C), 1 ml = 1 g
constexpr float MIN_SURPLUS_C = 0.5f;        // smaller surpluses aren't worth a flush (and are within sensor noise)
constexpr float MIN_SECONDS = 0.5f;          // shorter pulses mostly fill the group, not the boiler
// The flush button flushes to target from this far over it (settings flushButtonToTarget); closer, it runs the normal
// flush. Well clear of the PID's own overshoot after a flush (+0.5-0.75 °C on a Silvia), so a cleaning flush stays one.
constexpr float BUTTON_SURPLUS_C = 2.0f;
constexpr float MAX_SECONDS = 30.0f; // one press never empties more than this; press again if still hot
constexpr float DEFAULT_INLET_TEMP_C = 23.0f;
constexpr float DEFAULT_LOSS_FRACTION = 0.05f; // standing-loss guess while none has been learned (as the predictor)
// Share of the shown surplus that is really in the boiler (settings flushTargetFactor, 1.0 by default). Straight after
// steaming the water at the thermocouple is hotter than the boiler as a whole (and the group head keeps taking heat
// afterwards), so the reading overstates the surplus: on a Silvia two flushes from 106.5 and 108 °C, sized on the full
// reading, both undershot by ~2.2 °C. 0.85 then landed three runs on target; counting the 5 ml valve dump (~7 % of
// those 68 ml flushes) takes over part of that, so the same landings need 0.85 x 73 / 68 = 0.91 (the Silvia profile).
constexpr float SILVIA_SURPLUS_REALISED = 0.91f;

struct Plan {
    float ml = 0.0f;
    float seconds = 0.0f;
    const char *reason = nullptr; // set when no flush is needed or possible
};

// Temperatures as shown to the user; offset = the sensor-to-display temperature offset (boiler = shown + offset).
// flowMlPerS: pump flow into an open group (the 1-bar pump coefficient). lossW: the boiler's standing loss (<= 0: 5 % of
// the heater); holdS: how long the heater stays off after the flush. The boiler keeps losing heat through the flush and
// the hold, so that much less needs flushing (the second bench run undershot by ~2 °C without it). realised: the share
// of the surplus to take off (above); valveDumpMl: water the valve dumps as the flush ends, so the pump runs that much
// less.
inline Plan plan(float shownTemp, float shownTarget, float offset, float heaterW, float heatRate, float inletTemp,
                 float flowMlPerS, float lossW = 0.0f, float holdS = 0.0f, float realised = 1.0f, float valveDumpMl = 0.0f) {
    Plan p;
    if (!(heaterW > 0.0f) || !(heatRate > 0.0f)) {
        p.reason = "set heater power and heat-up rate first";
        return p;
    }
    if (!(flowMlPerS > 0.0f) || !std::isfinite(flowMlPerS)) {
        p.reason = "pump flow coefficients missing";
        return p;
    }
    const float surplus = shownTemp - shownTarget;
    if (!std::isfinite(surplus) || surplus < MIN_SURPLUS_C) {
        p.reason = "already at or below target";
        return p;
    }
    const float inlet = inletTemp > 0.0f ? inletTemp : DEFAULT_INLET_TEMP_C;
    // The boiler cools from current to target during the flush, so each ml takes heat at the average temperature.
    const float boilerAvg = (shownTemp + shownTarget) / 2.0f + offset;
    const float perMl = WATER_SPECIFIC_HEAT * (boilerAvg - inlet);
    if (perMl < WATER_SPECIFIC_HEAT * 10.0f) {
        p.reason = "tank water too warm to cool the boiler";
        return p;
    }
    const float capacity = heaterW / heatRate; // J/°C
    const float loss = lossW > 0.0f ? lossW : DEFAULT_LOSS_FRACTION * heaterW;
    const float surplusJ = surplus * (realised > 0.0f ? realised : 1.0f) * capacity;
    // The loss runs over the flush itself too, which depends on the volume: two passes converge.
    const auto pumpSeconds = [&](float s) {
        const float totalMl = std::max(surplusJ - loss * (s + std::max(holdS, 0.0f)), 0.0f) / perMl;
        return std::max(totalMl - std::max(valveDumpMl, 0.0f), 0.0f) / flowMlPerS;
    };
    float seconds = pumpSeconds(0.0f);
    for (int i = 0; i < 2; ++i)
        seconds = pumpSeconds(seconds);
    p.seconds = std::min(seconds, MAX_SECONDS);
    if (p.seconds < MIN_SECONDS) {
        p.seconds = 0.0f;
        p.reason = "surplus too small to flush";
        return p;
    }
    p.ml = p.seconds * flowMlPerS;
    return p;
}

} // namespace flush_to_target

#endif // FLUSH_TO_TARGET_H
