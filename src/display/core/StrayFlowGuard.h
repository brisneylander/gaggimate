#ifndef STRAY_FLOW_GUARD_H
#define STRAY_FLOW_GUARD_H

// Pump flow reported with nothing running. On a Silvia (2026-10-29) a brew stopped within a second left the controller
// reporting 5 ml/s for over a minute with no water moving; the heater's predictor counted that cold water and heated
// the boiler 93 -> 98 °C. It didn't reproduce in four quick start-and-stops, so whatever the cause, the display re-sends
// its full control state (pump off) once the flow has outlasted the pump's own wind-down, and again while it persists.
class StrayFlowGuard {
  public:
    static constexpr float MIN_FLOW = 0.5f;           // ml/s; the wind-down after a stop drops below this within ~1 s
    static constexpr unsigned long AFTER_MS = 3000UL; // how long it may last, and the retry interval

    // Called every control loop; true when the full state should be re-sent now.
    bool check(unsigned long nowMs, bool processRunning, float pumpFlow) {
        if (processRunning || !(pumpFlow > MIN_FLOW)) {
            since = 0;
            return false;
        }
        if (since == 0) {
            since = nowMs != 0 ? nowMs : 1;
            return false;
        }
        if (nowMs - since < AFTER_MS)
            return false;
        since = nowMs != 0 ? nowMs : 1; // retry every AFTER_MS while it persists
        return true;
    }

  private:
    unsigned long since = 0; // when stray flow was first seen, 0 = none
};

#endif // STRAY_FLOW_GUARD_H
