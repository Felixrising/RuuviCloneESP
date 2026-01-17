#include <Arduino.h>
#include <NimBLEDevice.h>
#include <array>
#include <string>
#include <esp_attr.h>
#include <esp_random.h>
#include <WiFi.h>

#include "config/board_config.h"
#include "sensors/sensor_select.h"

// Minimal Ruuvi RAWv2 (DF5) advertisement with sensor framework:
// - Fake data (default fallback)
// - NTC on ADC with optional ADC linearization LUT
// - ENV III (SHT30 + QMP6988)
// This is intended for Venus OS to recognize as a RuuviTag.

// Advertising interval in ms (0.625 ms units in BLE stack).
#ifndef ADV_INTERVAL_MS
#define ADV_INTERVAL_MS 1000
#endif

// BLE TX power level (fixed for reliability and range).
#ifndef BLE_TX_POWER
#define BLE_TX_POWER ESP_PWR_LVL_P3  // +3dBm: balanced range and power
#endif

// BLE TX power in dBm to encode into DF5 power field.
#ifndef BLE_TX_POWER_DBM
#define BLE_TX_POWER_DBM 3
#endif

// Firmware version exposed in scan response name.
#ifndef FW_VERSION_STR
#define FW_VERSION_STR "v3.31.1a"
#endif

// Ruuvi-aligned advertising intervals (ms) - continuous advertising mode.
// Deep sleep disabled, using interval-based continuous advertising instead.
#ifndef DEV_ADV_MS
#define DEV_ADV_MS 211
#endif
#ifndef FAST_ADV_MS
#define FAST_ADV_MS 1285
#endif
#ifndef SLOW_ADV_MS
#define SLOW_ADV_MS 8995
#endif

// Sensor polling interval (independent of advertising interval)
// Default 6000ms = 6 seconds, reasonable for environmental sensors
#ifndef SENSOR_POLL_INTERVAL_MS
#define SENSOR_POLL_INTERVAL_MS 6000
#endif

// Advertising burst duration (how long to advertise before stopping and restarting).
#ifndef ADV_BURST_MS
#define ADV_BURST_MS 300
#endif

// Operating mode selection:
//  0 = FAST_ONLY  - Always use FAST interval (1285ms)
//  1 = SLOW_ONLY  - Always use SLOW interval (8995ms)
//  2 = HYBRID     - Use FAST/SLOW based on boot time and movement (default)
#ifndef OPERATING_MODE
#define OPERATING_MODE 2
#endif

// HYBRID mode timing (only used if OPERATING_MODE == 2).
// Initial FAST mode duration: how long to stay in FAST mode after boot.
#ifndef FAST_MODE_INITIAL_MS
#define FAST_MODE_INITIAL_MS 60000  // 60 seconds
#endif

// Movement-triggered FAST mode extension: how long to stay in FAST after movement detected.
#ifndef FAST_MODE_MOVEMENT_MS
#define FAST_MODE_MOVEMENT_MS 60000  // 60 seconds
#endif

#ifndef JITTER_MS_MAX
#define JITTER_MS_MAX 10
#endif

#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 0
#endif

// When enabled, keep device awake to refresh LCD in all modes.
#ifndef DEBUG_LCD_FORCE_AWAKE
#define DEBUG_LCD_FORCE_AWAKE 0
#endif

// NOTE: Light sleep is NOT compatible with BLE advertising on ESP32.
// According to ESP-IDF documentation, light sleep powers down Wi-Fi and Bluetooth.
// The ESP32 BLE stack automatically uses Modem-sleep mode instead, which:
// - Keeps BLE radio active for advertising
// - Allows CPU to sleep between BLE events
// - Maintains connections automatically
// - Provides power savings without breaking BLE
//
// Reference: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/sleep_modes.html

namespace {

constexpr uint16_t kCompanyId = 0x0499; // Ruuvi
uint16_t gMeasurementSeq = 1;
uint8_t gMovementCounter = 0;
uint32_t gUptimeMs = 0;
uint32_t gFastUntilMs = 0;
uint16_t gLastBattMv = 0;
uint32_t gAdvRestartCount = 0;  // Track advertising restart events for diagnostics
bool gUsbState = false;
float gVf = 0.0f;
float gSf = 0.0f;
int gPowerScore = 0;
uint8_t gPowerState = 0;
float gVfPrev = 0.0f;

using SensorSample = ::SensorSample;

std::array<uint8_t, 6> parseMac(const std::string &mac_str) {
  // mac_str format "AA:BB:CC:DD:EE:FF"
  std::array<uint8_t, 6> mac = {0};
  size_t idx = 0;
  for (int i = 0; i < 6 && idx + 1 < mac_str.size(); ++i) {
    uint8_t hi = strtoul(mac_str.substr(idx, 2).c_str(), nullptr, 16);
    mac[i] = hi;
    idx += 3; // skip ':'
  }
  return mac;
}

inline void writeBE16(std::array<uint8_t, 24> &buf, size_t offset, int32_t val) {
  buf[offset] = (val >> 8) & 0xFF;
  buf[offset + 1] = val & 0xFF;
}

uint16_t encodeHumidity(float rh) {
  int32_t raw = lroundf(rh / 0.0025f);
  if (raw < 0)
    raw = 0;
  if (raw > 0xFFFF)
    raw = 0xFFFF;
  return static_cast<uint16_t>(raw);
}

int16_t encodeTemperature(float c) {
  int32_t raw = lroundf(c / 0.005f);
  if (raw < INT16_MIN)
    raw = INT16_MIN;
  if (raw > INT16_MAX)
    raw = INT16_MAX;
  return static_cast<int16_t>(raw);
}

uint16_t encodePressure(float hpa) {
  int32_t raw = lroundf(hpa * 100.0f - 50000.0f);
  if (raw < 0)
    raw = 0;
  if (raw > 0xFFFF)
    raw = 0xFFFF;
  return static_cast<uint16_t>(raw);
}

uint16_t encodePower(uint16_t battery_mv, int8_t tx_dbm) {
  if (battery_mv < 1600) {
    battery_mv = 1600;
  }
  if (battery_mv > 3646) {
    battery_mv = 3646;
  }
  int32_t batt_bits = battery_mv - 1600;
  if (batt_bits < 0)
    batt_bits = 0;
  if (batt_bits > 0x7FE)
    batt_bits = 0x7FE;

  int32_t tx_bits = (tx_dbm + 40) / 2;
  if (tx_bits < 0)
    tx_bits = 0;
  if (tx_bits > 0x1F)
    tx_bits = 0x1F;

  return static_cast<uint16_t>((batt_bits << 5) | tx_bits);
}

uint16_t mapBatteryMv(uint16_t real_mv) {
#if BATTERY_REPORT_MODE == 2
  if (real_mv < BATTERY_REAL_MIN_MV) {
    real_mv = BATTERY_REAL_MIN_MV;
  }
  if (real_mv > BATTERY_REAL_MAX_MV) {
    real_mv = BATTERY_REAL_MAX_MV;
  }
  const float t = (real_mv - BATTERY_REAL_MIN_MV) /
                  float(BATTERY_REAL_MAX_MV - BATTERY_REAL_MIN_MV);
  const float df5_mv =
      BATTERY_DF5_MIN_MV + t * (BATTERY_DF5_MAX_MV - BATTERY_DF5_MIN_MV);
  return static_cast<uint16_t>(lroundf(df5_mv));
#else
  if (real_mv < 1600) {
    return 1600;
  }
  if (real_mv > 3646) {
    return 3646;
  }
  return real_mv;
#endif
}

std::array<uint8_t, 24> buildDf5Payload(const SensorSample &sample,
                                        const std::array<uint8_t, 6> &mac) {
  std::array<uint8_t, 24> df5{};
  df5[0] = 0x05; // data format
  writeBE16(df5, 1, encodeTemperature(sample.temperature_c));
  writeBE16(df5, 3, encodeHumidity(sample.humidity_rh));
  writeBE16(df5, 5, encodePressure(sample.pressure_hpa));

  // Accel X/Y/Z in milli-g.
  writeBE16(df5, 7, sample.accel_x_mg);
  writeBE16(df5, 9, sample.accel_y_mg);
  writeBE16(df5, 11, sample.accel_z_mg);

  writeBE16(df5, 13, encodePower(sample.battery_mv, sample.tx_power_dbm));
  df5[15] = gMovementCounter;
  writeBE16(df5, 16, gMeasurementSeq++);

  // MAC big-endian.
  for (size_t i = 0; i < mac.size(); ++i) {
    df5[18 + i] = mac[i];
  }
  return df5;
}

std::string buildManufacturerData(const std::array<uint8_t, 24> &df5) {
  std::array<uint8_t, 26> payload{};
  payload[0] = kCompanyId & 0xFF;
  payload[1] = (kCompanyId >> 8) & 0xFF;
  memcpy(payload.data() + 2, df5.data(), df5.size());
  return std::string(reinterpret_cast<char *>(payload.data()), payload.size());
}

SensorSample readSensors() {
  static SensorSample last = sensors_read();
  SensorSample current = sensors_read();

  if (current.humidity_rh < 0.0f || current.humidity_rh > 100.0f ||
      current.temperature_c < -40.0f || current.temperature_c > 85.0f) {
    if (DEBUG_SERIAL) {
      Serial.printf("Sensor invalid t/h (t=%.2f h=%.2f); using last\n",
                    current.temperature_c,
                    current.humidity_rh);
    }
    current = last;
  } else {
    last = current;
  }

  current.battery_mv = mapBatteryMv(board_read_battery_mv());
  current.tx_power_dbm = BLE_TX_POWER_DBM;
  board_read_accel_mg(current.accel_x_mg, current.accel_y_mg, current.accel_z_mg);
  return current;
}

uint8_t batteryPercentFromMv(uint16_t mv) {
  if (mv <= 3000) {
    return 0;
  }
  if (mv >= 4200) {
    return 100;
  }
  return static_cast<uint8_t>(((mv - 3000) * 100) / 1200);
}

bool updateMovementCounter(const SensorSample &sample) {
  // Simple motion detection: if accel delta exceeds threshold, increment.
  constexpr int16_t kDeltaThresholdMg = 120; // tune as needed
  static int16_t last_ax = 0;
  static int16_t last_ay = 0;
  static int16_t last_az = 0;
  static uint32_t last_movement_ms = 0;
  static bool first_call = true;

  // Initialize on first call to avoid false trigger
  if (first_call) {
    last_ax = sample.accel_x_mg;
    last_ay = sample.accel_y_mg;
    last_az = sample.accel_z_mg;
    first_call = false;
    return false;
  }

  const int16_t dx = abs(sample.accel_x_mg - last_ax);
  const int16_t dy = abs(sample.accel_y_mg - last_ay);
  const int16_t dz = abs(sample.accel_z_mg - last_az);
  const int16_t max_delta = max(dx, max(dy, dz));
  const uint32_t now = millis();

  if (DEBUG_SERIAL && max_delta >= kDeltaThresholdMg) {
    Serial.printf("[MOVEMENT] Delta: dx=%d dy=%d dz=%d max=%d (threshold=%d)\n",
                  dx, dy, dz, max_delta, kDeltaThresholdMg);
  }

  if (max_delta >= kDeltaThresholdMg && (now - last_movement_ms) > 300) {
    gMovementCounter++;
    last_movement_ms = now;
    last_ax = sample.accel_x_mg;
    last_ay = sample.accel_y_mg;
    last_az = sample.accel_z_mg;
    return true;
  }

  last_ax = sample.accel_x_mg;
  last_ay = sample.accel_y_mg;
  last_az = sample.accel_z_mg;
  return false;
}

uint32_t jitterMs() {
  return esp_random() % (JITTER_MS_MAX + 1);
}

bool detectUsbFromBattery(uint16_t batt_mv) {
  const int override = board_usb_override_mode();
  if (override == 0) {
    return false;
  }
  if (override == 1) {
    return true;
  }

  // EWMA filtering and trend state machine.
  // Higher ALPHA = faster response, lower = more smoothing
#ifndef VBAT_ALPHA
#define VBAT_ALPHA 0.05f  // Slower response, less jitter
#endif
#ifndef VBAT_BETA
#define VBAT_BETA 0.04f  // Slower slope tracking
#endif
#ifndef VBAT_SPIKE_MV
#define VBAT_SPIKE_MV 30  // Increased: ignore bigger spikes
#endif
#ifndef VBAT_T_CHARGE
#define VBAT_T_CHARGE 8.0f  // Increased: require stronger charging signal
#endif
#ifndef VBAT_T_DISCHARGE
#define VBAT_T_DISCHARGE 3.0f  // Increased: require stronger discharge signal
#endif
#ifndef VBAT_SCORE_MAX
#define VBAT_SCORE_MAX 12  // Increased: need more evidence to switch
#endif

  enum PowerState : uint8_t { POWER_UNKNOWN = 0, POWER_CHARGING = 1, POWER_DISCHARGING = 2 };

  // Initialize filter on first sample.
  if (gVf == 0.0f) {
    gVf = batt_mv;
    gVfPrev = gVf;
  }

  // Compute delta for spike detection.
  const int16_t delta_raw = static_cast<int16_t>(batt_mv) - static_cast<int16_t>(gLastBattMv);
  const bool spike = (gLastBattMv != 0) && (abs(delta_raw) > VBAT_SPIKE_MV);

  // Update filtered voltage.
  gVf = gVf + VBAT_ALPHA * (batt_mv - gVf);

  // Estimate slope (mV/s) from filtered voltage change.
  // Assuming this is called regularly (every ~1-9 seconds).
  const float slope = (gVf - gVfPrev);  // mV change since last call
  gVfPrev = gVf;
  gSf = gSf + VBAT_BETA * (slope - gSf);
  const float slope_mV_min = gSf * 60.0f;  // Approximate mV/min

  if (!spike) {
    if (slope_mV_min > VBAT_T_CHARGE) {
      gPowerScore += 2;
    } else if (slope_mV_min < -VBAT_T_DISCHARGE) {
      gPowerScore -= 1;
    } else {
      if (gPowerScore > 0) {
        gPowerScore -= 1;
      } else if (gPowerScore < 0) {
        gPowerScore += 1;
      }
    }
    if (gPowerScore > VBAT_SCORE_MAX) {
      gPowerScore = VBAT_SCORE_MAX;
    }
    if (gPowerScore < -VBAT_SCORE_MAX) {
      gPowerScore = -VBAT_SCORE_MAX;
    }
  }

  if (gPowerScore >= VBAT_SCORE_MAX) {
    gPowerState = POWER_CHARGING;
  } else if (gPowerScore <= -VBAT_SCORE_MAX) {
    gPowerState = POWER_DISCHARGING;
  }

  // Near-full handling.
  if (gVf > 4150.0f && slope_mV_min > -2.0f) {
    gPowerState = POWER_CHARGING;
  }

  // Update last raw voltage.
  gLastBattMv = batt_mv;

  if (gPowerState == POWER_CHARGING) {
    gUsbState = true;
  } else if (gPowerState == POWER_DISCHARGING) {
    gUsbState = false;
  }
  return gUsbState;
}

uint16_t intervalUnitsFromMs(uint32_t ms) {
  uint32_t units = (ms * 1000) / 625;
  if (units < 32) {
    units = 32;
  }
  if (units > 16384) {
    units = 16384;
  }
  return static_cast<uint16_t>(units);
}

void startAdvertising(NimBLEAdvertising *adv,
                      const SensorSample &sample,
                      uint32_t adv_ms) {
  const auto mac = parseMac(NimBLEDevice::getAddress().toString());
  const auto df5 = buildDf5Payload(sample, mac);
  const auto mfg = buildManufacturerData(df5);

  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.setManufacturerData(mfg);

  NimBLEAdvertisementData srData;
  std::string name = std::string("Ruuvi-ESP32 ") + FW_VERSION_STR;
  srData.setName(name); // visible to scanners on active scan

  // Battery Service (0x180F) with level percent.
  uint8_t batt_pct = batteryPercentFromMv(sample.battery_mv);
  std::string batt_payload(reinterpret_cast<char *>(&batt_pct), sizeof(batt_pct));
  srData.setServiceData(NimBLEUUID((uint16_t)0x180F), batt_payload);

  // Update advertising data (this can be done while advertising is running)
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(srData);
  
  // BLE spec requires random jitter to avoid collisions with other advertisers
  // Set min/max to create a range, BLE stack picks random interval in this window
  uint16_t minIntervalUnits = intervalUnitsFromMs(adv_ms);
  uint16_t maxIntervalUnits = intervalUnitsFromMs(adv_ms + JITTER_MS_MAX);
  adv->setMinInterval(minIntervalUnits);
  adv->setMaxInterval(maxIntervalUnits);

  // Only start if not already advertising (keep it running continuously)
  if (!adv->isAdvertising()) {
    adv->start();
  }

  if (DEBUG_SERIAL) {
    Serial.printf("T=%.2fC H=%.2f%% P=%.2fhPa Batt=%umV Accel=[%d,%d,%d] Tx=%ddBm Mov=%u\n",
                  sample.temperature_c,
                  sample.humidity_rh,
                  sample.pressure_hpa,
                  sample.battery_mv,
                  sample.accel_x_mg,
                  sample.accel_y_mg,
                  sample.accel_z_mg,
                  sample.tx_power_dbm,
                  gMovementCounter);
  }
}

} // namespace

void setup() {
  if (DEBUG_SERIAL) {
    Serial.begin(115200);
    delay(50);
     Serial.println("\n=== Ruuvi DF5 Advertiser (Continuous Mode, BLE Modem-sleep) ===");
    const char *op_mode_str = (OPERATING_MODE == 0) ? "FAST_ONLY" :
                              (OPERATING_MODE == 1) ? "SLOW_ONLY" : "HYBRID";
    Serial.printf("Operating Mode: %s\n", op_mode_str);
    if (OPERATING_MODE == 2) {
      Serial.printf("Hybrid Timing: FAST_INITIAL=%lus, FAST_MOVEMENT=%lus\n",
                    FAST_MODE_INITIAL_MS / 1000,
                    FAST_MODE_MOVEMENT_MS / 1000);
    }
    Serial.printf("Intervals: DEV=%lums, FAST=%lums, SLOW=%lums\n",
                  DEV_ADV_MS, FAST_ADV_MS, SLOW_ADV_MS);
    Serial.printf("Sensor Poll: %lums (%.1fs)\n",
                  SENSOR_POLL_INTERVAL_MS, SENSOR_POLL_INTERVAL_MS / 1000.0f);
     Serial.printf("BLE TX Power: %ddBm\n", BLE_TX_POWER_DBM);
     Serial.println("Power Management: Automatic BLE Modem-sleep (CPU sleeps, BLE radio active)");
    Serial.println("========================================================\n");
  }

  board_init();
  board_wake_pulse_led();

  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true, true);

  NimBLEDevice::init("Ruuvi-ESP32");
  NimBLEDevice::setPower(BLE_TX_POWER);
  
  if (DEBUG_SERIAL) {
    Serial.print("BLE MAC: ");
    Serial.println(NimBLEDevice::getAddress().toString().c_str());
    Serial.printf("BLE TX Power: %ddBm\n", BLE_TX_POWER_DBM);
  }

  sensors_init();
}

void loop() {
  static uint32_t last_adv_ms = 0;
  static uint32_t last_status_ms = 0;
  static uint32_t last_sensor_poll_ms = 0;
  static uint32_t last_adv_health_check_ms = 0;
  static SensorSample cached_sample = {};  // Cached sensor reading
  static bool force_immediate_adv = false;  // Force next advertisement immediately after movement
  static bool first_loop = true;  // Track first loop iteration
  const uint32_t now_ms = millis();
  
  // Update uptime
  gUptimeMs = now_ms;

  // Determine mode
  const uint16_t batt_mv_raw = board_read_battery_mv();
  const bool usb = detectUsbFromBattery(batt_mv_raw);
  
  // DEV mode logic: explicit opt-in only (no auto-trigger from USB)
  const bool force_awake = DEBUG_LCD && DEBUG_LCD_FORCE_AWAKE;
  const bool dev_mode = board_dev_mode_enabled() || force_awake;
  
  // Determine FAST/SLOW mode based on OPERATING_MODE
  bool fast_mode, slow_mode;
  const char *mode_label;
  uint32_t adv_interval_ms;
  
  if (dev_mode) {
    fast_mode = false;
    slow_mode = false;
    mode_label = "DEV";
    adv_interval_ms = DEV_ADV_MS;
  } else if (OPERATING_MODE == 0) {
    // FAST_ONLY mode
    fast_mode = true;
    slow_mode = false;
    mode_label = "FAST";
    adv_interval_ms = FAST_ADV_MS;
  } else if (OPERATING_MODE == 1) {
    // SLOW_ONLY mode
    fast_mode = false;
    slow_mode = true;
    mode_label = "SLOW";
    adv_interval_ms = SLOW_ADV_MS;
  } else {
    // HYBRID mode (default)
    fast_mode = (gUptimeMs < FAST_MODE_INITIAL_MS || gUptimeMs < gFastUntilMs);
    slow_mode = !fast_mode;
    mode_label = fast_mode ? "FAST" : "SLOW";
    adv_interval_ms = fast_mode ? FAST_ADV_MS : SLOW_ADV_MS;
  }
  
  // Periodic status output (every 10s)
  if (DEBUG_SERIAL && (now_ms - last_status_ms >= 10000)) {
    last_status_ms = now_ms;
    const char *op_mode_str = (OPERATING_MODE == 0) ? " [FAST_ONLY]" :
                              (OPERATING_MODE == 1) ? " [SLOW_ONLY]" : " [HYBRID]";
    Serial.printf("[STATUS] Mode=%s%s interval=%lums uptime=%lus seq=%u batt=%umV USB=%s adv_restarts=%lu\n",
                  mode_label,
                  op_mode_str,
                  adv_interval_ms,
                  gUptimeMs / 1000,
                  gMeasurementSeq,
                  batt_mv_raw,
                  usb ? "YES" : "NO",
                  gAdvRestartCount);
    if (OPERATING_MODE == 2) {
      const uint32_t fast_countdown_s = (gFastUntilMs > gUptimeMs) ? (gFastUntilMs - gUptimeMs) / 1000 : 0;
      Serial.printf("[HYBRID] fast_until=%lus, FAST_INITIAL=%lus, FAST_MOVEMENT=%lus\n",
                    fast_countdown_s,
                    FAST_MODE_INITIAL_MS / 1000,
                    FAST_MODE_MOVEMENT_MS / 1000);
    }
  }

  // Aggressive advertising health check (every 1s) - ensure it's still running
  // This catches any cases where BLE stack stops advertising unexpectedly
  if ((now_ms - last_adv_health_check_ms >= 1000) || last_adv_health_check_ms == 0) {
    last_adv_health_check_ms = now_ms;
    auto *adv_check = NimBLEDevice::getAdvertising();
    if (!adv_check->isAdvertising()) {
      gAdvRestartCount++;
      // CRITICAL: Advertising stopped! Restart immediately
      if (DEBUG_SERIAL) {
        Serial.printf("[ADV] CRITICAL: Advertising stopped at uptime=%lus! (restart #%lu) Restarting immediately...\n",
                      now_ms / 1000, gAdvRestartCount);
      }
      // Force restart with current cached sample and current interval
      startAdvertising(adv_check, cached_sample, adv_interval_ms);
      
      // Verify it actually started
      delay(100);
      if (!adv_check->isAdvertising()) {
        if (DEBUG_SERIAL) {
          Serial.println("[ADV] ERROR: Failed to restart advertising! Attempting full BLE restart...");
        }
        // Last resort: stop and restart BLE stack
        NimBLEDevice::deinit(true);
        delay(500);
        NimBLEDevice::init("Ruuvi-ESP32");
        NimBLEDevice::setPower(BLE_TX_POWER);
        startAdvertising(adv_check, cached_sample, adv_interval_ms);
      }
    }
  }

  // Poll sensors at fixed interval (decoupled from advertising)
  if ((now_ms - last_sensor_poll_ms >= SENSOR_POLL_INTERVAL_MS) || last_sensor_poll_ms == 0) {
    last_sensor_poll_ms = now_ms;
    cached_sample = readSensors();
    if (DEBUG_SERIAL) {
      Serial.printf("[SENSOR] Polled at uptime=%lus (interval=%lums)\n", 
                    now_ms / 1000, SENSOR_POLL_INTERVAL_MS);
    }
  }

  // Force immediate advertising on first loop iteration
  if (first_loop) {
    first_loop = false;
    force_immediate_adv = true;
    if (DEBUG_SERIAL) {
      Serial.println("[ADV] First loop - starting advertising immediately");
    }
  }

  // Advertise at mode-appropriate interval (or immediately if movement detected or first loop)
  if (force_immediate_adv || (now_ms - last_adv_ms >= adv_interval_ms)) {
    last_adv_ms = now_ms;
    force_immediate_adv = false;
    
    // Update LCD on advertisement (not every second)
    if (DEBUG_LCD) {
      const uint32_t fast_countdown_ms =
          (gFastUntilMs > gUptimeMs) ? (gFastUntilMs - gUptimeMs) : 0;
      board_debug_refresh(mode_label, usb, fast_countdown_ms, 
                          gMeasurementSeq, gMovementCounter);
    }
    
    auto *adv = NimBLEDevice::getAdvertising();
    // Use cached sensor reading instead of polling every advertisement
    SensorSample sample = cached_sample;
    
    // Update movement counter (always), but only trigger FAST mode in HYBRID mode
    if (updateMovementCounter(sample) && OPERATING_MODE == 2) {
      if (gUptimeMs + FAST_MODE_MOVEMENT_MS > gFastUntilMs) {
        gFastUntilMs = gUptimeMs + FAST_MODE_MOVEMENT_MS;
        force_immediate_adv = true;  // Trigger next advertisement immediately
        if (DEBUG_SERIAL) {
          Serial.printf("[MOVEMENT] Triggered FAST mode until uptime=%lus (current=%lus, duration=%lus)\n",
                        gFastUntilMs / 1000,
                        gUptimeMs / 1000,
                        FAST_MODE_MOVEMENT_MS / 1000);
        }
      }
    }
    
    // Update advertising data and restart if needed
    // Keep advertising running continuously - don't stop it!
    startAdvertising(adv, sample, adv_interval_ms);
    
    if (DEBUG_SERIAL) {
      Serial.printf("[ADV] Mode=%s interval=%lums tx=%ddBm uptime=%lus fast_until=%lus seq=%u batt=%umV\n",
                    mode_label,
                    adv_interval_ms,
                    BLE_TX_POWER_DBM,
                    gUptimeMs / 1000,
                    gFastUntilMs / 1000,
                    gMeasurementSeq - 1,
                    batt_mv_raw);
    }
    
    // Small delay to ensure advertising starts properly
    delay(50);
  }
  
  // NOTE: Light sleep is NOT compatible with BLE advertising on ESP32.
  // According to ESP-IDF documentation:
  // "In Deep-sleep and Light-sleep modes, the wireless peripherals are powered down."
  // "Wi-Fi and Bluetooth connections are not maintained in Deep-sleep or Light-sleep mode."
  //
  // The ESP32 BLE stack automatically uses Modem-sleep mode, which:
  // - Keeps the BLE radio active for advertising
  // - Allows CPU to sleep between BLE events
  // - Maintains BLE connections automatically
  // - Provides significant power savings without breaking BLE
  //
  // We use a small delay here to prevent busy-waiting. The BLE stack handles
  // actual power management automatically via Modem-sleep.
  delay(10);
}

