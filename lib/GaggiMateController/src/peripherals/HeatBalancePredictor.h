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
    static constexpr float REHEAT_START_J = 300.0f;       // start a full-power reheat once this much is owed (~0.16 °C)
    static constexpr float REHEAT_MAX_IDLE_S = 180.0f;    // a water event ends this long after the last flow at the latest
    static constexpr float COAST_BAND = 0.2f;             // °C: coasting ends once the sensor is this close to setpoint

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

    // Full-power reheat after a flush or shot, by energy count rather than by temperature: at the start of a water event
    // owed = heat capacity x (setpoint - predicted temperature); from then on the water's heat and the standing loss add
    // to it and the heater's output subtracts. Running flat out until nothing is owed delivers exactly what the boiler
    // lost, as fast as the element allows. (Owing setpoint - predicted at every step over-heats: the sensor keeps falling
    // after the water delay, as the flush's effect is spread out, so the prediction reads low.) Only active during a water
    // event, so warm-up and holding stay with the PID. A deliberate cooling flush from above the setpoint starts in
    // credit and ends near zero, so it doesn't start a reheat.
    //
    // Once nothing is owed the heater coasts at the standing-loss power (which keeps the count at zero) until the sensor
    // has caught up with the heat still in the element and wall, then the PID takes over. Handing back straight away lets
    // the PID see the still-low sensor and add heat the boiler doesn't need. Coasting gives up after 3 dead times.
    bool reheatWanted() const { return reheatActive; }
    bool isCoasting() const { return coasting; }
    // Heater duty (0..1) the water event wants for the next step, or < 0 when the PID should run as usual. A reheat runs
    // flat out but sizes its last step to what is still owed: a whole extra second at full power is ~0.6 °C on a Silvia.
    float eventDuty() const {
        if (heaterWatts <= 0.0f || (!reheatActive && !coasting))
            return -1.0f;
        const float holding = std::min(lossWatts / heaterWatts, 1.0f);
        if (!reheatActive)
            return holding;
        const float step = lastDt > 0.0f ? lastDt : 1.0f;
        return std::min(1.0f, std::max(holding, (energyOwed / step + lossWatts) / heaterWatts));
    }
    float getEnergyOwed() const { return energyOwed; } // J (negative = in credit), 0 outside a water event

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
    bool eventActive = false;
    float sinceFlow = 0.0f;
    float lastFlow = 0.0f; // ml/s at the previous step: its fall to 0 is the valve's dump
    bool reheatActive = false;
    bool coasting = false;
    float lastDt = 0.0f;
    float energyOwed = 0.0f;

    float heaterWatts = 0.0f;
    float heatCapacity = 0.0f; // J/°C
    float deadTime = 0.0f;
    float waterDeadTime = 0.0f;
    float inletTemp = 23.0f;
};

#endif // HEATBALANCEPREDICTOR_H
