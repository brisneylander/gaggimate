#ifndef TEMPERATURESENSOR_H
#define TEMPERATURESENSOR_H

#include "HeaterSafety.h"

// The sensors' backstop shutdown uses the safety layer's limit (one place for the number).
constexpr double MAX_SAFE_TEMP = HeaterSafety::MAX_SAFE_C;

class TemperatureSensor {
  public:
    virtual ~TemperatureSensor() = default;
    virtual float read();
    virtual bool isErrorState();
    virtual void setup();
};

#endif // TEMPERATURESENSOR_H
