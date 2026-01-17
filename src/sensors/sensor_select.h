#pragma once

// Sensor profiles
#define SENSOR_PROFILE_FAKE 0
#define SENSOR_PROFILE_NTC 1
#define SENSOR_PROFILE_ENV3 2

#ifndef SENSOR_PROFILE
#define SENSOR_PROFILE SENSOR_PROFILE_FAKE
#endif

#if SENSOR_PROFILE == SENSOR_PROFILE_ENV3
#include "sensor_env3.h"
#elif SENSOR_PROFILE == SENSOR_PROFILE_NTC
#include "sensor_ntc.h"
#else
#include "sensor_fake.h"
#endif

inline bool sensors_init() {
  return sensor_init();
}

inline SensorSample sensors_read() {
  return sensor_read();
}
