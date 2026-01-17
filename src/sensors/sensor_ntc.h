#pragma once

#include "sensor_interface.h"
#include <Arduino.h>
#include "ntc_lut.h"

#ifndef NTC_ADC_PIN
#define NTC_ADC_PIN 1
#endif
#ifndef NTC_SERIES_OHMS
#define NTC_SERIES_OHMS 10000.0f
#endif
#ifndef NTC_NOMINAL_OHMS
#define NTC_NOMINAL_OHMS 10000.0f
#endif
#ifndef NTC_NOMINAL_TEMP_C
#define NTC_NOMINAL_TEMP_C 25.0f
#endif
#ifndef NTC_BETA
#define NTC_BETA 3950.0f
#endif

inline uint16_t ntc_linearize_adc(uint16_t raw) {
#ifdef USE_FULL_NTC_LUT
  if (raw >= 4096)
    raw = 4095;
  return static_cast<uint16_t>(ADC_LUT[raw]);
#else
  constexpr uint16_t step = 128;
  uint16_t idx = raw / step;
  if (idx >= 32)
    return kAdcLut[32];
  uint16_t base = kAdcLut[idx];
  uint16_t next = kAdcLut[idx + 1];
  uint16_t rem = raw % step;
  return base + ((next - base) * rem) / step;
#endif
}

inline float ntc_read_celsius() {
  uint16_t raw = analogRead(NTC_ADC_PIN);
  uint16_t corrected = ntc_linearize_adc(raw);
  float voltage_ratio = corrected / 4095.0f;
  float resistance = NTC_SERIES_OHMS * voltage_ratio / (1.0f - voltage_ratio + 1e-6f);
  float steinhart = resistance / NTC_NOMINAL_OHMS;
  steinhart = logf(steinhart);
  steinhart /= NTC_BETA;
  steinhart += 1.0f / (NTC_NOMINAL_TEMP_C + 273.15f);
  steinhart = 1.0f / steinhart;
  return steinhart - 273.15f;
}

inline bool sensor_init() {
  pinMode(NTC_ADC_PIN, INPUT);
  return true;
}

inline SensorSample sensor_read() {
  SensorSample s{
      .temperature_c = ntc_read_celsius(),
      .humidity_rh = 50.0f,
      .pressure_hpa = 1013.25f,
      .battery_mv = 0,
      .tx_power_dbm = 0,
      .accel_x_mg = 0,
      .accel_y_mg = 0,
      .accel_z_mg = 0,
  };
  return s;
}
