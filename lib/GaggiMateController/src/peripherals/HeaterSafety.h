#ifndef HEATERSAFETY_H
#define HEATERSAFETY_H

#include <cmath>
#include <cstdint>

// Hard limits on the heater, independent of the control code: Heater::writeOutput() asks permits() before every
// switch-on, so no PID, predictor or autotune path can bypass it. Once tripped it stays off until a restart.
//
// What it adds to the sensor's own checks (over MAX_SAFE_TEMP, repeated read failures) is the failure those can't see:
// a thermocouple that has come off the boiler reads room temperature or a slow fall, so the PID heats flat out and
// only the machine's thermal fuse would stop it.
//   - No rise: near full power for a while, no water flowing, and the reading hasn't gone up. Brew temperatures allow
//     60 s (a Silvia's sensor reacts ~20 s after the heater, then rises ~0.6 °C/s); steam temperatures allow 180 s,
//     since steaming milk holds the heater flat out while the reading holds or falls.
//   - Jumps: three readings within 30 s that moved more than 5 °C in a second (a loose junction, interference).
//   - Continuous full power longer than 10 minutes (a cold start to steam takes ~4).
class HeaterSafety {
  public:
    enum class Reason : uint8_t { None = 0, NoRise = 1, Jumps = 2, FullPowerTooLong = 3 };

    static constexpr float HIGH_DUTY = 0.8f;
    static constexpr float NO_RISE_C = 2.0f;
    static constexpr uint32_t NO_RISE_BREW_MS = 60000;
    static constexpr uint32_t NO_RISE_STEAM_MS = 180000;
    static constexpr float STEAM_SETPOINT_C = 110.0f; // setpoints from here up count as steam
    static constexpr float JUMP_C = 5.0f;
    static constexpr int JUMPS_TO_TRIP = 3;
    static constexpr uint32_t JUMP_WINDOW_MS = 30000;
    static constexpr float FULL_DUTY = 0.95f;
    static constexpr uint32_t FULL_POWER_MAX_MS = 600000;

    // Called once per control step with the reading, the duty about to be applied (0..1), whether water is flowing
    // and the setpoint. Returns false once tripped.
    bool observe(uint32_t nowMs, float temperature, float duty, bool flowing, float setpoint) {
        if (tripped != Reason::None)
            return false;
        if (!std::isfinite(temperature))
            return true; // the sensor's own fault handling covers unreadable values
        checkJumps(nowMs, temperature);
        checkNoRise(nowMs, temperature, duty, flowing, setpoint);
        checkFullPower(nowMs, duty);
        lastTemp = temperature;
        lastMs = nowMs;
        haveLast = true;
        return tripped == Reason::None;
    }

    bool permits() const { return tripped == Reason::None; }
    Reason reason() const { return tripped; }
    static const char *describe(Reason r) {
        switch (r) {
        case Reason::NoRise:
            return "heater on but temperature not rising (sensor off the boiler?)";
        case Reason::Jumps:
            return "temperature reading jumping (loose thermocouple?)";
        case Reason::FullPowerTooLong:
            return "heater at full power too long";
        default:
            return "none";
        }
    }

  private:
    void trip(Reason r) {
        if (tripped == Reason::None)
            tripped = r;
    }

    void checkJumps(uint32_t nowMs, float temperature) {
        if (!haveLast)
            return;
        const float elapsedS = static_cast<float>(nowMs - lastMs) / 1000.0f;
        if (elapsedS <= 0.0f || elapsedS > 5.0f)
            return; // a gap in readings isn't a jump
        if (std::fabs(temperature - lastTemp) > JUMP_C * std::fmax(elapsedS, 1.0f)) {
            if (jumpCount == 0 || nowMs - firstJumpMs > JUMP_WINDOW_MS) {
                jumpCount = 0;
                firstJumpMs = nowMs;
            }
            if (++jumpCount >= JUMPS_TO_TRIP)
                trip(Reason::Jumps);
        }
    }

    void checkNoRise(uint32_t nowMs, float temperature, float duty, bool flowing, float setpoint) {
        if (duty < HIGH_DUTY || flowing) {
            noRiseActive = false;
            return;
        }
        if (!noRiseActive) {
            noRiseActive = true;
            noRiseStartMs = nowMs;
            noRiseLowest = temperature;
            return;
        }
        // Rise measured from the lowest reading in the window: a flush's dip, then the climb back, is a rise.
        noRiseLowest = std::fmin(noRiseLowest, temperature);
        if (temperature - noRiseLowest >= NO_RISE_C) {
            noRiseStartMs = nowMs;
            noRiseLowest = temperature;
            return;
        }
        const uint32_t limit = setpoint >= STEAM_SETPOINT_C ? NO_RISE_STEAM_MS : NO_RISE_BREW_MS;
        if (nowMs - noRiseStartMs >= limit)
            trip(Reason::NoRise);
    }

    void checkFullPower(uint32_t nowMs, float duty) {
        if (duty < FULL_DUTY) {
            fullActive = false;
            return;
        }
        if (!fullActive) {
            fullActive = true;
            fullStartMs = nowMs;
        } else if (nowMs - fullStartMs >= FULL_POWER_MAX_MS) {
            trip(Reason::FullPowerTooLong);
        }
    }

    Reason tripped = Reason::None;
    bool haveLast = false;
    float lastTemp = 0.0f;
    uint32_t lastMs = 0;
    int jumpCount = 0;
    uint32_t firstJumpMs = 0;
    bool noRiseActive = false;
    uint32_t noRiseStartMs = 0;
    float noRiseLowest = 0.0f;
    bool fullActive = false;
    uint32_t fullStartMs = 0;
};

#endif // HEATERSAFETY_H
