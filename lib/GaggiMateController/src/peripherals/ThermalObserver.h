#ifndef THERMALOBSERVER_H
#define THERMALOBSERVER_H

#include <deque>

// Estimates the boiler water temperature *now* from a small thermal model, corrected by the (slow, late) sensor.
//
// Model, fitted to a Rancilio Silvia (scripts/fit_plant.py, test/test_heat_balance_predictor): the element feeds the
// water with a ~21 s time constant; the group head trades heat with the water over ~2.4 min and carries the standing
// loss; flush or shot water replaces boiler water with tank water; the thermocouple sees the water through a short
// transport delay and a first-order lag. Capacities scale with the configured one (heater power / full-power heat-up
// rate): element 11 %, water 71 %, group 45 % of it. The sensor's delays scale with the water dead time (12 s on the
// Silvia: 4.6 s transport + 15 s lag).
//
// Each step the model runs forward, then the difference between the sensor it predicts and the real reading (the
// innovation) nudges the states: the water and element by a small share, and a slow bias term for heat the model
// doesn't know about (losses, a wrong tank temperature), so the estimate settles on the sensor at rest. The gains
// are fixed (a steady-state observer rather than a full Kalman filter), chosen in the simulator.
class ThermalObserver {
  public:
    static constexpr float WATER_SPECIFIC_HEAT = 4.18f; // J/(g·°C)
    static constexpr float ELEMENT_SHARE = 0.109f;      // of the configured capacity
    static constexpr float WATER_SHARE = 0.707f;
    static constexpr float GROUP_SHARE = 0.447f;
    static constexpr float ELEMENT_TAU_S = 20.9f; // element -> water
    static constexpr float GROUP_TAU_S = 143.6f;  // water <-> group, seen from the group
    static constexpr float SENSOR_DELAY_PER_WATER_DEAD = 4.6f / 12.0f;
    static constexpr float SENSOR_LAG_PER_WATER_DEAD = 15.0f / 12.0f;
    static constexpr float ROOM_C = 22.0f;

    // Observer gains per second of innovation (°C error between sensor and predicted sensor).
    static constexpr float GAIN_WATER = 0.05f;
    static constexpr float GAIN_ELEMENT = 0.02f;
    static constexpr float GAIN_BIAS_W = 4.0f; // W of unexplained heat per °C·s of innovation

    // heaterWatts / fullPowerRate give the capacity; waterDeadTimeS the sensor's delays; any <= 0 disables it.
    void configure(float heaterWatts, float fullPowerRate, float waterDeadTimeS, float inletTemp);
    bool isEnabled() const { return capacity > 0.0f; }
    // Start every state at this temperature (a boiler at rest), holding lossWatts.
    void reset(float temp, float lossWatts);

    // Advance dt seconds: heater duty 0..1 over dt, flow ml/s of tank water entering, extraWaterMl drawn at once (the
    // valve's dump), and the sensor reading. Returns the water temperature estimate now.
    float update(float dt, float duty, float flow, float extraWaterMl, float measuredTemp);
    float getWater() const { return water; }
    // Where the water is heading once the heat stored in the element has reached it (energy-weighted mix of element and
    // water, less the holding power's share of the element's lead). Controlling on the water now ignores that heat and
    // overshoots ~2 °C after a flush.
    float getSettling() const;
    // Share (0..1) of the group head's heat deficit (below its resting temperature) counted in getSettling(): after a
    // flush the cooled group keeps pulling the water down for minutes, which the heater can start putting back early.
    float groupWeight = 0.0f;
    float getElement() const { return element; }
    float getGroup() const { return group; }
    float getBiasWatts() const { return biasW; }
    float getPredictedSensor() const { return sensor; }

  private:
    float capacity = 0.0f, heaterW = 0.0f, inletTemp = 23.0f;
    float delayS = 0.0f, lagS = 1.0f;
    float element = 0.0f, water = 0.0f, group = 0.0f, sensor = 0.0f;
    float lossPerC = 0.0f; // W/°C of (water - room), through the group
    float biasW = 0.0f;
    bool primed = false;
    std::deque<std::pair<float, float>> pipe; // (dt, water temperature) on its way to the sensor
    float pipeTime = 0.0f;
};

#endif // THERMALOBSERVER_H
