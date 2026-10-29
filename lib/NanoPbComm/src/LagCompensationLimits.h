#ifndef LAG_COMPENSATION_LIMITS_H
#define LAG_COMPENSATION_LIMITS_H

#include <algorithm>
#include <cmath>

// Accepted ranges for the brew-boiler lag compensation settings (HeaterLagCompensation). The display clamps what it
// stores and the controller clamps what it receives, so neither trusts the other; the web UI mirrors these numbers
// (web/src/pages/Settings/tabs/LagCompensationSection.jsx). Anything <= 0 or not a number means "off".
namespace lag_limits {

struct Range {
    float min;
    float max;
};

constexpr Range DERIVATIVE_FILTER_S{0.5f, 10.0f}; // longer filters delay the D term's braking (worse overshoot)
constexpr Range INTEGRAL_ZONE_C{0.5f, 5.0f};      // below ~0.5 degC the band is inside sensor noise
constexpr Range SENSOR_DEAD_TIME_S{2.0f, 60.0f};  // sensor in the water ... very slow shell-mounted thermocouple
constexpr Range WATER_DEAD_TIME_S{1.0f, 60.0f};   // flush/shot water to half the sensor's dip; 0 = SENSOR_DEAD_TIME_S
constexpr Range HEATER_W{200.0f, 2500.0f};        // espresso boiler elements
constexpr Range HEAT_RATE_C_PER_S{0.05f, 3.0f};   // large HX/dual boilers ... small fast boilers (Silvia ~0.6)
constexpr Range STANDING_LOSS_W{0.0f, 300.0f};    // holding power at brew temperature
constexpr Range INLET_TEMP_C{5.0f, 70.0f};        // tank water; tanks beside the boiler warm up (40 degC on a Silvia)
constexpr float DEFAULT_INLET_TEMP_C = 23.0f;     // used while the inlet temperature is 0 (not set)
constexpr Range TANK_FILL_C{5.0f, 40.0f};         // display only: fresh tank water for the tank estimate
// Machine-specific values (settings, 0 = the neutral default; the Silvia profile in the web UI fills them in):
// water a 3-way valve dumps into the drip tray when a flush or shot ends. It has left the boiler too, but the pump flow
// reading doesn't include it (4.8-5.7 g on a Silvia over six weighed flushes, 2026-10-29). 0 = none counted.
constexpr Range VALVE_DUMP_ML{0.5f, 30.0f};
// Heater safety: how long near full power with no rise may last (HeaterSafety.h; 0 = its 60 / 300 s defaults).
constexpr Range NO_RISE_BREW_S{30.0f, 300.0f};
constexpr Range NO_RISE_STEAM_S{60.0f, 900.0f};
// Display only: flush to target's share of the shown surplus (0 = 1.0) and heater hold after it (0 = 30 s); the tank
// estimate's warming above the fill temperature (0 = 18 °C) and its time constant in minutes (0 = 120).
constexpr Range FLUSH_TARGET_FACTOR{0.5f, 1.2f};
constexpr Range FLUSH_TARGET_HOLD_S{1.0f, 120.0f};
constexpr Range TANK_RISE_C{1.0f, 40.0f};
constexpr Range TANK_WARM_MIN{10.0f, 600.0f};

// 0 for off (<= 0 or not finite), otherwise clamped into the range.
inline float clampOrOff(float value, Range range) {
    if (!std::isfinite(value) || value <= 0.0f)
        return 0.0f;
    return std::min(std::max(value, range.min), range.max);
}

} // namespace lag_limits

#endif // LAG_COMPENSATION_LIMITS_H
