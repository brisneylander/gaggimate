#ifndef TANK_TEMPERATURE_H
#define TANK_TEMPERATURE_H

#include <algorithm>
#include <cmath>

// Estimate of the tank water temperature, for the heater's predictor and flush to target (each ml drawn in takes
// heat in proportion to boiler - tank). A tank beside the boiler warms while the machine is on: on a Silvia ~25 °C
// fresh, ~40 °C after a few hours (2026-10-28/29 bench, thermometer). Fresh water is at the fill temperature, set
// per season (Brisbane: ~15-18 °C in winter, ~24-28 °C in summer). The constants are first guesses from those few
// readings; refine them with a thermometer over a session.
//
// Refills reset it: by hand, or when a water-level sensor (Alba ToF) climbs back to full after being low.
class TankTemperature {
  public:
    static constexpr float WARM_RISE_C = 18.0f;     // on for hours: settles this far above the fill temperature
    static constexpr float WARM_TAU_S = 7200.0f;    // ~2 h time constant while the machine is on
    static constexpr float COOL_TAU_S = 10800.0f;   // ~3 h back towards the fill temperature in standby / off
    static constexpr int REFILL_LOW_PERCENT = 90;   // the level must have been below this ...
    static constexpr int REFILL_FULL_PERCENT = 100; // ... and reach this again to count as a refill

    // Fill temperature (°C); <= 0 turns the estimate off (the caller keeps its fixed tank setting).
    void configure(float fillC) {
        const float f = std::isfinite(fillC) && fillC > 0.0f ? fillC : 0.0f;
        if (f == fill)
            return;
        // A new season's value: keep how far the tank has warmed above the old fill temperature.
        const float warmed = fill > 0.0f && primed ? temp - fill : 0.0f;
        fill = f;
        temp = fill + std::max(warmed, 0.0f);
        primed = fill > 0.0f;
    }
    bool isEnabled() const { return fill > 0.0f; }
    float get() const { return temp; }

    // Fresh water in the tank.
    void refilled() {
        if (isEnabled())
            temp = fill;
    }

    // Advance dt seconds; machineOn = out of standby (the boiler is hot and warming the tank).
    void update(float dt, bool machineOn) {
        if (!isEnabled() || !(dt > 0.0f))
            return;
        const float target = machineOn ? fill + WARM_RISE_C : fill;
        const float tau = machineOn ? WARM_TAU_S : COOL_TAU_S;
        temp += (target - temp) * std::min(dt / tau, 1.0f);
    }

    // Water level in percent from a level sensor, each time it is read. Returns true when it counted as a refill.
    bool onWaterLevel(int percent) {
        if (percent < REFILL_LOW_PERCENT)
            wasLow = true;
        if (wasLow && percent >= REFILL_FULL_PERCENT) {
            wasLow = false;
            refilled();
            return isEnabled();
        }
        return false;
    }

  private:
    float fill = 0.0f;
    float temp = 0.0f;
    bool primed = false;
    bool wasLow = false;
};

#endif // TANK_TEMPERATURE_H
