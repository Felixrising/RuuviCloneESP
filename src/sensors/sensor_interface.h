#pragma once

#include <stdint.h>

struct SensorSample {
  float temperature_c;
  float humidity_rh;
  float pressure_hpa;
  uint16_t battery_mv;
  int8_t tx_power_dbm;
  int16_t accel_x_mg;
  int16_t accel_y_mg;
  int16_t accel_z_mg;
};
