#pragma once

#include "sensor_interface.h"
#include <Wire.h>
#include "M5UnitENV.h"

static SHT3X gSht3x;
static QMP6988 gQmp6988;
static bool gEnv3Ready = false;

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 32
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 33
#endif

inline bool sensor_init() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);

  bool qmp_ok = gQmp6988.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, I2C_SDA_PIN, I2C_SCL_PIN, 400000U);
  if (!qmp_ok) {
    qmp_ok = gQmp6988.begin(&Wire, QMP6988_SLAVE_ADDRESS_H, I2C_SDA_PIN, I2C_SCL_PIN, 400000U);
  }
  bool sht_ok = gSht3x.begin(&Wire, SHT3X_I2C_ADDR, I2C_SDA_PIN, I2C_SCL_PIN, 400000U);
  if (!sht_ok) {
    sht_ok = gSht3x.begin(&Wire, 0x45, I2C_SDA_PIN, I2C_SCL_PIN, 400000U);
  }
  gEnv3Ready = qmp_ok && sht_ok;
  return gEnv3Ready;
}

inline SensorSample sensor_read() {
  SensorSample s{
      .temperature_c = 0.0f,
      .humidity_rh = 0.0f,
      .pressure_hpa = 1013.25f,
      .battery_mv = 0,
      .tx_power_dbm = 0,
      .accel_x_mg = 0,
      .accel_y_mg = 0,
      .accel_z_mg = 0,
  };

  if (gEnv3Ready && gSht3x.update() && gQmp6988.update()) {
    s.temperature_c = gSht3x.cTemp;
    s.humidity_rh = gSht3x.humidity;
    s.pressure_hpa = gQmp6988.pressure / 100.0f; // Pa -> hPa
  }
  return s;
}
