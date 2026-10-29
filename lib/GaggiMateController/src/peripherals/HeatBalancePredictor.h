#ifndef HEATBALANCEPREDICTOR_H
#define HEATBALANCEPREDICTOR_H

#include <algorithm>
#include <deque>

// Smith-predictor style lag compensation for a boiler whose thermocouple sees heater power only after a
// dead time. It integrates a heat balance (heater in; fresh water and standing losses out) and reports the
// part of it the sensor hasn't seen yet, so the PID can run on measured + correction instead of a reading
// that is ~20 s old. Only the model's last dead-time window is used, so model errors cannot accumulate.
//
// Parameters come straight from a full-power heat-up: heater wattage, the steady heating rate it produces
// (°C/s, autotune's k') and the dead time before the sensor reacts. Heat capacity = wattage / rate.
//
// Water drawn in by a flush or shot reaches the sensor sooner than heater heat: on a Silvia the dip starts after ~5 s
// and is half way down by ~12 s, while heater heat takes ~20 s. With one shared delay the predictor keeps counting a
// flush the sensor already shows, i.e. counts it twice, and over-heats; so water has its own delay. Set it to about the
// half-way time: a delay as short as the first reaction drops the flush while the sensor is still falling.
class HeatBalancePredictor {
  public:
    static constexpr float WATER_SPECIFIC_HEAT = 4.18f;   // J/(g·°C), 1 ml = 1 g
    static constexpr float MAX_CORRECTION = 15.0f;        // °C, bound on how far the prediction may lead the sensor
    static constexpr float LOSS_LEARN_BAND = 1.0f;        // °C, losses are only learned this close to setpoint
    static constexpr float LOSS_LEARN_TAU = 300.0f;       // s, time constant of the standing-loss estimate
    static constexpr float DEFAULT_LOSS_FRACTION = 0.05f; // first-run loss guess: ~5 % of the element at brew temp

    // Any parameter <= 0 disables the predictor (correction stays 0).
    // waterDeadTimeSeconds <= 0 uses deadTimeSeconds for water too (the original single-delay model).
    void configure(float heaterWatts, float fullPowerRate, float deadTimeSeconds, float inletTemp = 23.0f,
                   float waterDeadTimeSeconds = 0.0f);
    bool isEnabled() const { return heatCapacity > 0.0f; }
    float getDeadTime() const { return deadTime; }
    float getWaterDeadTime() const { return waterDeadTime; }
    void reset();
    // Starting point for the learned standing loss, applied once: a value saved from an earlier run, or <= 0 for
    // DEFAULT_LOSS_FRACTION of the heater. Starting from zero would read the holding power as heat in flight and
    // sag the boiler by ~deadTime x holding power / capacity until the loss is relearned.
    void seedLoss(float savedWatts);

    // Advance the model by dt seconds and return the correction (°C) to add to the measured temperature.
    // duty: heater on-fraction 0..1 over dt; flow: ml/s of fresh water entering the boiler.
    float update(float dt, float duty, float flow, float measuredTemp, float setpoint);
    float getCorrection() const { return correction; }
    float getLossWatts() const { return lossWatts; }

  private:
    struct Step {
        float dt;
        float heat;  // °C the heater (net of standing loss) added this step; reaches the sensor after deadTime
        float water; // °C fresh water removed this step; reaches the sensor after waterDeadTime
    };
    std::deque<Step> window; // the last max(deadTime, waterDeadTime) of model steps, oldest first
    float windowTime = 0.0f;
    float correction = 0.0f;
    float lossWatts = 0.0f;
    bool lossSeeded = false;
    float lastFlow = 0.0f; // ml/s at the previous step: its fall to 0 is the valve's dump

    float heaterWatts = 0.0f;
    float heatCapacity = 0.0f; // J/°C
    float deadTime = 0.0f;
    float waterDeadTime = 0.0f;
    float inletTemp = 23.0f;
};

#endif // HEATBALANCEPREDICTOR_H
