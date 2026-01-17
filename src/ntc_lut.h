#pragma once

// ADC linearization lookup table for NTC mode.
// By default, a small 33-point table (128-count steps) is used to keep
// firmware size low. For best accuracy, use a full 4096-point table generated
// per-board (see ntc_lut_full.h and the e-tinkers reference).
//
// Reference LUT source and discussion:
// https://raw.githubusercontent.com/e-tinkers/ntc-thermistor-with-arduino-and-esp32/refs/heads/master/ntc_3950.ino

#pragma once
#include <stdint.h>

#ifdef USE_FULL_NTC_LUT
// Provide a 4096-entry float/int table in ntc_lut_full.h:
//   - Copy the ADC_LUT array from the e-tinkers sketch above, or generate your
//     own via https://github.com/e-tinkers/esp32-adc-calibrate
//   - Set USE_FULL_NTC_LUT in build_flags
#include "ntc_lut_full.h"
#else
// 33-entry coarse LUT (raw 0..4095 in 128-count steps); linear interpolate.
static const uint16_t kAdcLut[33] = {
    0,    120,  240,  360,  480,  600,  720,  840,  960,  1080, 1200,
    1320, 1440, 1560, 1680, 1800, 1920, 2040, 2160, 2280, 2400, 2520,
    2640, 2760, 2880, 3000, 3120, 3240, 3360, 3480, 3600, 3720, 3840};
#endif

