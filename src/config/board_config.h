#pragma once

#include <stdint.h>

// Board profiles
#define BOARD_PROFILE_GENERIC 0
#define BOARD_PROFILE_M5STICKCPLUS2 1

#ifndef BOARD_PROFILE
#define BOARD_PROFILE BOARD_PROFILE_GENERIC
#endif

#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
#include <M5Unified.h>
#include <driver/rtc_io.h>
#endif

#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 0
#endif

// LCD debug overlay (shows USB state)
#ifndef DEBUG_LCD
#define DEBUG_LCD 0
#endif

// LCD brightness (0-255, where 0=off, 255=max)
#ifndef LCD_BRIGHTNESS
#define LCD_BRIGHTNESS 3  // ~1% brightness (3/255 ≈ 1.2%)
#endif

// USB mode override:
//  -1 = auto detect (default)
//   0 = force battery mode (ignore USB detection)
//   1 = force USB connected (but still FAST/SLOW based on time/movement)
#ifndef USB_MODE_OVERRIDE
#define USB_MODE_OVERRIDE -1
#endif

// DEV mode (explicit opt-in for development/testing):
//   0 = disabled (default) - Use FAST/SLOW modes regardless of USB
//   1 = enabled - Use DEV mode (211ms continuous advertising)
#ifndef DEV_MODE_ENABLE
#define DEV_MODE_ENABLE 0
#endif

// Optional LED pulse on wake to confirm RTC wake cycles.
#ifndef WAKE_PULSE_MS
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
#define WAKE_PULSE_MS 200
#else
#define WAKE_PULSE_MS 0
#endif
#endif

// M5StickC Plus2 needs HOLD (GPIO4) asserted to stay on after RTC wake.
#ifndef BOARD_POWER_HOLD_ENABLE
#define BOARD_POWER_HOLD_ENABLE 1
#endif
#ifndef BOARD_POWER_HOLD_PIN
#define BOARD_POWER_HOLD_PIN 4
#endif

// Battery reporting defaults per board profile.
#ifndef BATTERY_REPORT_MODE
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
#define BATTERY_REPORT_MODE 2
#else
#define BATTERY_REPORT_MODE 1
#endif
#endif

#ifndef BATTERY_REAL_MIN_MV
#define BATTERY_REAL_MIN_MV 3000
#endif
#ifndef BATTERY_REAL_MAX_MV
#define BATTERY_REAL_MAX_MV 4200
#endif

#ifndef BATTERY_DF5_MIN_MV
#define BATTERY_DF5_MIN_MV 1900
#endif
#ifndef BATTERY_DF5_MAX_MV
#define BATTERY_DF5_MAX_MV 3600
#endif

inline void board_init() {
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
  if (BOARD_POWER_HOLD_ENABLE) {
    rtc_gpio_hold_dis(static_cast<gpio_num_t>(BOARD_POWER_HOLD_PIN));
    pinMode(BOARD_POWER_HOLD_PIN, OUTPUT);
    digitalWrite(BOARD_POWER_HOLD_PIN, HIGH);
    rtc_gpio_hold_en(static_cast<gpio_num_t>(BOARD_POWER_HOLD_PIN));
  }
  auto cfg = M5.config();
  cfg.serial_baudrate = DEBUG_SERIAL ? 115200 : 0;
  M5.begin(cfg);
  if (DEBUG_LCD) {
    M5.Display.setBrightness(LCD_BRIGHTNESS);
    M5.Display.wakeup();
    M5.Display.setRotation(1);
    M5.Display.clear(BLACK);
  } else {
    M5.Display.setBrightness(0);
    M5.Display.sleep();
  }
  setCpuFrequencyMhz(80);
#endif
}

inline void board_wake_pulse_led() {
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
  if (WAKE_PULSE_MS == 0) {
    return;
  }
  M5.Power.setLed(1);
  delay(WAKE_PULSE_MS);
  M5.Power.setLed(0);
#endif
}

inline int board_read_battery_level() {
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
  return M5.Power.getBatteryLevel();
#else
  return -1;
#endif
}

inline bool board_is_usb_powered() {
#if USB_MODE_OVERRIDE == 0
  return false;
#elif USB_MODE_OVERRIDE == 1
  return true;
#endif
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
  // This board's charging detection is unreliable.
  return false;
#else
  return false;
#endif
}

inline int board_usb_override_mode() {
  return USB_MODE_OVERRIDE;
}

inline bool board_dev_mode_enabled() {
  return (DEV_MODE_ENABLE == 1);
}

inline uint16_t board_read_battery_mv() {
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
  uint32_t mv = M5.Power.getBatteryVoltage();
  if (mv > 0 && mv < 10000) {
    return static_cast<uint16_t>(mv);
  }
  int level = M5.Power.getBatteryLevel(); // 0-100
  if (level > 0) {
    return static_cast<uint16_t>(3000 + (level * 12)); // 3.0V..4.2V
  }
  // Do not trust charging state on this device.
#endif
  return 3300;
}

inline void board_debug_refresh(const char *mode_label,
                               bool usb_connected,
                               uint32_t fast_countdown_ms,
                               uint16_t seq,
                               uint8_t mov) {
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
  if (!DEBUG_LCD) {
    return;
  }

  // Pre-read hardware to avoid delays while screen is active
  const uint16_t mv = board_read_battery_mv();
  const int lvl = board_read_battery_level();

  // Force brightness and ensure it's awake
  M5.Display.wakeup();
  M5.Display.setBrightness(LCD_BRIGHTNESS);
  
  M5.Display.startWrite();
  M5.Display.setCursor(0, 0);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(WHITE, BLACK);
  
  // Clear the screen once per refresh inside startWrite to avoid flicker
  M5.Display.clear(BLACK);

  M5.Display.printf("U:%s M:%-4s\n", usb_connected ? "Y" : "N", mode_label);
  M5.Display.printf("S:%-5u V:%-3u\n", seq, mov);
  
  if (fast_countdown_ms > 0) {
    M5.Display.printf("F:%-3us\n", static_cast<unsigned>(fast_countdown_ms / 1000));
  } else {
    M5.Display.printf("F:---\n");
  }

  M5.Display.printf("B:%-4umV\n", mv);
  M5.Display.printf("L:%-3d%%\n", lvl);
  
  M5.Display.endWrite();
#endif
}

// board_debug_off() removed: keeping LCD on for development.

inline void board_read_accel_mg(int16_t &x_mg, int16_t &y_mg, int16_t &z_mg) {
  x_mg = 0;
  y_mg = 0;
  z_mg = 0;
#if BOARD_PROFILE == BOARD_PROFILE_M5STICKCPLUS2
  float ax, ay, az;
  if (M5.Imu.getAccelData(&ax, &ay, &az)) {
    x_mg = static_cast<int16_t>(ax * 1000.0f);
    y_mg = static_cast<int16_t>(ay * 1000.0f);
    z_mg = static_cast<int16_t>(az * 1000.0f);
  }
#endif
}
