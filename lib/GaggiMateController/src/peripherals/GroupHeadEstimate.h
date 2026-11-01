#ifndef GROUPHEADESTIMATE_H
#define GROUPHEADESTIMATE_H

#include <algorithm>
#include <cmath>

// Tracks the group head's temperature from the boiler reading, and the heat it is soaking up beyond its standing loss.
//
// After a cold start the boiler reaches its setpoint in 1.5-2.5 min, but the group head (a ~1.3 kJ/°C brass mass on a
// Silvia) is still cold and keeps drawing heat for ~10 min. Neither the PID nor the heat-balance predictor knows: the
// predictor counts the heater's heat as in the water, the integral only acts within its zone, so the boiler sagged
// 2-3.5 °C below the setpoint and crept back over 7-10 min (logged 2026-11-02). This estimate feeds that uptake to the
// heater as a feedforward and to the predictor as a loss; once the group head has caught up, the uptake is zero and
// nothing changes. Model (fitted to the logged warm-ups, scripts/fit_warmup.py):
//   group heat capacity Cg, water -> group coupling kg; the group head loses lossPerK x (group - room) to the room.
class GroupHeadEstimate {
  public:
    static constexpr float ROOM_C = 22.0f;

    // Any parameter <= 0 disables it (uptake stays 0).
    void configure(float heatCapacity, float coupling, float lossPerK) {
        const bool valid = std::isfinite(heatCapacity) && std::isfinite(coupling) && std::isfinite(lossPerK) &&
                           heatCapacity > 0.0f && coupling > 0.0f && lossPerK >= 0.0f;
        cg = valid ? heatCapacity : 0.0f;
        kg = valid ? coupling : 0.0f;
        loss = valid ? lossPerK : 0.0f;
    }
    bool isEnabled() const { return cg > 0.0f; }
    void setLossPerK(float lossPerK) {
        if (std::isfinite(lossPerK) && lossPerK >= 0.0f)
            loss = lossPerK;
    }

    // boiler: the reading (sensor temperature). Starts at the first reading (a machine left alone has its group head
    // at the boiler's temperature) and then follows the model, Standby included.
    void update(float dt, float boiler) {
        if (!isEnabled() || !(dt > 0.0f) || !std::isfinite(boiler))
            return;
        if (!started) {
            group = boiler;
            started = true;
        }
        const float in = kg * (boiler - group);
        const float out = loss * (group - ROOM_C);
        group += (in - out) * dt / cg;
        uptake = std::max(in - out, 0.0f);
    }
    // W the group head is storing right now (beyond what it passes on to the room); 0 when it has caught up.
    float getUptakeWatts() const { return uptake; }
    float getTemperature() const { return group; }

  private:
    float cg = 0.0f, kg = 0.0f, loss = 0.0f;
    float group = 0.0f;
    float uptake = 0.0f;
    bool started = false;
};

#endif // GROUPHEADESTIMATE_H
