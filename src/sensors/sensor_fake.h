#pragma once

#include "sensor_interface.h"
#include <Arduino.h>

inline bool sensor_init() {
  return true;
}

inline SensorSample sensor_read() {
  const float base = 22.0f;
  float delta = (millis() / 1000 % 6) * 0.5f; // 0..2.5
  SensorSample s{
      .temperature_c = base + delta,
      .humidity_rh = 45.0f + delta,
      .pressure_hpa = 1013.25f,
      .battery_mv = 0,
      .tx_power_dbm = 0,
      .accel_x_mg = 0,
      .accel_y_mg = 0,
      .accel_z_mg = 0,
  };
  return s;
}
