#ifndef HEATBALANCEPREDICTOR_H
#define HEATBALANCEPREDICTOR_H

#include <deque>

// Smith-predictor style lag compensation for a boiler whose thermocouple sees heater power only after a
// dead time. It integrates a heat balance (heater in; fresh water and standing losses out) and reports the
// part of it the sensor hasn't seen yet, so the PID can run on measured + correction instead of a reading
// that is ~20 s old. Only the model's last dead-time window is used, so model errors cannot accumulate.
//
// Parameters come straight from a full-power heat-up: heater wattage, the steady heating rate it produces
// (°C/s, autotune's k') and the dead time before the sensor reacts. Heat capacity = wattage / rate.
class HeatBalancePredictor {
  public:
    static constexpr float WATER_SPECIFIC_HEAT = 4.18f; // J/(g·°C), 1 ml = 1 g
    static constexpr float MAX_CORRECTION = 15.0f;      // °C, bound on how far the prediction may lead the sensor
    static constexpr float LOSS_LEARN_BAND = 1.0f;      // °C, losses are only learned this close to setpoint
    static constexpr float LOSS_LEARN_TAU = 300.0f;     // s, time constant of the standing-loss estimate

    // Any parameter <= 0 disables the predictor (correction stays 0).
    void configure(float heaterWatts, float fullPowerRate, float deadTimeSeconds, float inletTemp = 23.0f);
    bool isEnabled() const { return heatCapacity > 0.0f; }
    float getDeadTime() const { return deadTime; }
    void reset();

    // Advance the model by dt seconds and return the correction (°C) to add to the measured temperature.
    // duty: heater on-fraction 0..1 over dt; flow: ml/s of fresh water entering the boiler.
    float update(float dt, float duty, float flow, float measuredTemp, float setpoint);
    float getCorrection() const { return correction; }
    float getLossWatts() const { return lossWatts; }

  private:
    struct Step {
        float dt;
        float rise; // °C the model says this step added
    };
    std::deque<Step> window; // the last dead-time worth of model steps, oldest first
    float windowTime = 0.0f;
    float windowRise = 0.0f;
    float correction = 0.0f;
    float lossWatts = 0.0f;

    float heaterWatts = 0.0f;
    float heatCapacity = 0.0f; // J/°C
    float deadTime = 0.0f;
    float inletTemp = 23.0f;
};

#endif // HEATBALANCEPREDICTOR_H
