# RuuviTag ESP32 Emulator

ESP32-based firmware that emulates a RuuviTag environmental sensor, advertising temperature, humidity, pressure, and movement data via Bluetooth Low Energy (BLE) in Ruuvi Data Format 5 (RAWv2). Designed to be recognized by Victron Venus OS and Ruuvi mobile apps.

## Features

- **Ruuvi DF5 (RAWv2) Compliant:** Advertises as manufacturer ID `0x0499` with correct payload format
- **Multiple Sensor Support:** ENV III (SHT30 + QMP6988), NTC thermistor, or fake data for testing
- **Smart Operating Modes:** FAST_ONLY, SLOW_ONLY, or HYBRID with movement-triggered adaptation
- **Power Management:** Light sleep, reduced CPU frequency (80MHz), configurable BLE TX power (+3dBm)
- **Movement Detection:** IMU-based motion tracking with 120mg threshold
- **USB Detection:** Voltage-trend state machine for reliable USB connection detection
- **LCD Debug Display:** Real-time status monitoring on M5StickC Plus2 screen
- **Extensible Architecture:** Easy to add new boards and sensors

## Supported Hardware

### M5StickC Plus2 + ENV III Sensor (Primary)
- **Board:** M5StickC Plus2 (ESP32-S3)
- **Sensor:** M5Unit-ENV III (SHT30 + QMP6988) via HY2.0-4P port
- **I2C:** SDA=GPIO32, SCL=GPIO33
- **Features:** IMU, battery management, LCD display, LED indicator

### ESP32-S3 Zero (Generic)
- **Board:** ESP32-S3 DevKit
- **Sensor:** NTC thermistor or fake data
- **I2C:** SDA=GPIO8, SCL=GPIO9 (configurable)

## Quick Start

### Build and Upload (M5StickC Plus2)

```bash
# Build and upload firmware
pio run -t upload -e m5stickcplus2

# Monitor serial output (if DEBUG_SERIAL enabled)
pio device monitor -b 115200
```

### Build and Upload (ESP32-S3 Zero)

```bash
# Clean, erase, and upload
pio run -t clean -e esp32s3
pio run -t erase -e esp32s3
pio run -t upload -e esp32s3
```

## Operating Modes

The firmware supports three operating modes controlled by `OPERATING_MODE` build flag:

| Mode | Interval | Power | Battery Life* | Use Case |
|------|----------|-------|---------------|----------|
| **FAST_ONLY** (0) | 1.285s | ~5-8mA | 25-40h | Development, always-responsive |
| **SLOW_ONLY** (1) | 8.995s | ~2-3mA | 65-100h | Long-term monitoring, max battery |
| **HYBRID** (2) | Smart | ~3-6mA | 35-65h | **General purpose (default)** |

*Based on 200mAh battery with light sleep enabled*

### HYBRID Mode Behavior

HYBRID mode intelligently switches between FAST (1.285s) and SLOW (8.995s) intervals:

1. **Boot → FAST mode** for initial discovery (`FAST_MODE_INITIAL_MS`, default 60s)
2. **FAST → SLOW** after timeout with no movement
3. **Movement detected → FAST mode** for `FAST_MODE_MOVEMENT_MS` (default 60s)
4. **No movement → SLOW** after timeout

## Configuration

### Operating Mode Selection

Edit `platformio.ini` under `[env:m5stickcplus2]` build_flags:

```ini
# Choose ONE:
-DOPERATING_MODE=0  # FAST_ONLY - Always 1285ms
-DOPERATING_MODE=1  # SLOW_ONLY - Always 8995ms  
-DOPERATING_MODE=2  # HYBRID - Smart switching (default)
```

### HYBRID Mode Timing

```ini
# Only relevant if OPERATING_MODE=2
-DFAST_MODE_INITIAL_MS=60000   # 60s: FAST mode after boot
-DFAST_MODE_MOVEMENT_MS=60000  # 60s: FAST mode after movement
```

### Debug Options

```ini
-DDEBUG_LCD=1                  # Enable LCD status display
-DLCD_BRIGHTNESS=3             # LCD brightness 0-255 (3 ≈ 1%)
-DDEBUG_SERIAL=1               # Enable serial logging
-DDEV_MODE_ENABLE=1            # Force DEV mode (211ms continuous)
-DDEBUG_LCD_FORCE_AWAKE=1      # Stay awake for LCD updates
```

### Power Management Tuning

```ini
# Light sleep (enabled by default, saves ~80% power)
-DENABLE_LIGHT_SLEEP=1

# BLE TX power (default: +3dBm for ~15m range)
-DBLE_TX_POWER=ESP_PWR_LVL_P3
-DBLE_TX_POWER_DBM=3

# USB detection sensitivity (if flickering)
-DVBAT_T_CHARGE=10.0           # Higher = less sensitive
-DVBAT_T_DISCHARGE=4.0         # Higher = less sensitive
-DVBAT_SCORE_MAX=15            # Higher = slower switching
```

## LCD Debug Display

When `DEBUG_LCD=1` is enabled, the M5StickC Plus2 displays real-time status:

```
U:N M:SLOW     ← USB detected (Y/N), Operating Mode
S:142 V:8      ← Sequence number, Movement counter
F:---          ← Fast mode countdown (seconds, if in HYBRID)
B:4125mV       ← Battery voltage
L:85%          ← Battery level
```

Updates on each advertisement cycle (1.3s FAST, 9s SLOW).

## Sensor Profiles

Select sensor via `SENSOR_PROFILE` build flag:

| Profile | Sensor | Configuration |
|---------|--------|---------------|
| **0** (FAKE) | Dummy data | No hardware required, for testing |
| **1** (NTC) | NTC Thermistor | GPIO1 ADC, 50K 3950B NTC |
| **2** (ENV3) | M5Unit-ENV III | SHT30 + QMP6988 via I2C |

## Power Consumption

Estimated current draw with M5StickC Plus2 + ENV III sensor:

| Configuration | Average Current | Battery Life (200mAh) |
|--------------|-----------------|----------------------|
| FAST_ONLY + Light Sleep | 5-8mA | 25-40 hours |
| SLOW_ONLY + Light Sleep | 2-3mA | 65-100 hours |
| HYBRID (low activity) | 3-4mA | 50-65 hours |
| HYBRID (moderate) | 4-5mA | 40-50 hours |
| DEBUG_LCD enabled | +2-3mA | -20% battery life |

### Power Saving Features

- **Light Sleep:** Sleeps between advertisements (~80% power reduction)
- **Reduced CPU:** 80MHz instead of 240MHz (~66% reduction)
- **WiFi Disabled:** Always off
- **LCD Off:** Disabled in production mode (unless debugging)
- **Fixed BLE TX Power:** +3dBm for balanced range and power

## Battery & USB Detection

### Battery Reporting

Two modes via `BATTERY_REPORT_MODE`:

1. **Clip Mode (1):** Clamps real voltage to DF5 limits (1600-3646mV)
2. **Map Mode (2, default for M5StickC Plus2):** Maps Li-ion range (3.0-4.2V) to DF5 safe range (1.9-3.6V)

### USB Connection Detection

Uses filtered voltage-trend state machine:
- Monitors battery voltage slope (mV/min)
- EWMA filtering to reject jitter and spikes
- Score-based hysteresis prevents oscillation
- Reliable detection despite unreliable M5 charging state

**Note:** USB detection is for informational purposes only and does not affect operating mode by default (use `DEV_MODE_ENABLE=1` to force DEV mode).

## Movement Detection

- **Accelerometer:** 6-axis IMU on M5StickC Plus2
- **Threshold:** 120mg (milligravity) delta in any axis
- **Behavior:** Increments movement counter and triggers FAST mode in HYBRID mode
- **Debounce:** 300ms minimum between detections

## Advertisement Intervals

Aligned with official RuuviTag firmware:

| Mode | Interval | Source |
|------|----------|--------|
| DEV (Test) | 211ms ± 0-10ms | Test firmware |
| FAST (Default) | 1285ms ± 0-10ms | Default firmware |
| SLOW (Long-life) | 8995ms ± 0-10ms | Long-life firmware |

Reference: [Ruuvi BLE Advertisements](https://docs.ruuvi.com/communication/bluetooth-advertisements)

## Ruuvi DF5 Payload

24-byte payload encoding:

| Offset | Field | Encoding | Range |
|--------|-------|----------|-------|
| 0 | Format | 0x05 | DF5 identifier |
| 1-2 | Temperature | 0.005°C steps | -163.835 to +163.835°C |
| 3-4 | Humidity | 0.0025% steps | 0 to 163.8375% |
| 5-6 | Pressure | 1 Pa steps | 500 to 1155.35 hPa |
| 7-8 | Accel X | 1 mg steps | -32.767 to +32.767 g |
| 9-10 | Accel Y | 1 mg steps | -32.767 to +32.767 g |
| 11-12 | Accel Z | 1 mg steps | -32.767 to +32.767 g |
| 13-14 | Power Info | 11bit battery + 5bit TX | 1.6-3.646V, -40 to +20dBm |
| 15 | Movement | Counter | 0-254 |
| 16-17 | Sequence | Counter | 0-65534 |
| 18-23 | MAC | Big-endian | Device BLE address |

## Project Structure

```
RuuviCloneESP/
├── src/
│   ├── main.cpp                    # Core logic and BLE advertising
│   ├── config/
│   │   └── board_config.h          # Board-specific hardware abstraction
│   ├── sensors/
│   │   ├── sensor_interface.h      # Common sensor interface
│   │   ├── sensor_select.h         # Sensor selection logic
│   │   ├── sensor_fake.h           # Dummy sensor (testing)
│   │   ├── sensor_ntc.h            # NTC thermistor support
│   │   └── sensor_env3.h           # ENV III (SHT30 + QMP6988)
│   ├── ntc_lut.h                   # 33-point NTC lookup table
│   └── ntc_lut_full.h              # Optional 4096-point LUT
├── docs/
│   ├── ruuvitag-emulation-notes.md # DF5 format documentation
│   ├── timing-profiles.md          # Operating mode guide
│   └── power-management-implementation.md
├── references/
│   ├── ntc_3950.ino                # NTC reference implementation
│   └── README.md                   # Reference links
├── platformio.ini                  # Build configuration
└── README.md                       # This file
```

## Known Limitations

### M5StickC Plus2 Specific

1. **Deep Sleep Unreliable:** RTC wake from deep sleep doesn't work consistently. Light sleep is used instead.
2. **Charging State Unreliable:** `M5.Power.isCharging()` is not trustworthy. Use voltage-trend detection instead.
3. **GPIO4 HOLD Pin:** Must be held HIGH via `rtc_gpio_hold_en()` or device powers off on battery.

### General

- No BLE connection support (advertisement-only, like real RuuviTags)
- Movement counter rolls over at 255
- Sequence counter rolls over at 65535
- Battery life with light sleep is significantly less than deep sleep (25-100h vs potential 500h+)

## Troubleshooting

### Device Not Advertising

1. Check BLE scanner (Ruuvi mobile app or nRF Connect)
2. Look for device name: `Ruuvi-ESP32 v3.31.1a`
3. Enable serial debug: `-DDEBUG_SERIAL=1`
4. Check MAC address in serial output matches BLE scanner

### LCD Not Updating

1. Verify `DEBUG_LCD=1` in build flags
2. Check `LCD_BRIGHTNESS` setting (default: 3)
3. In battery modes, LCD updates once per advertisement (1-9s depending on mode)
4. Use `DEBUG_LCD_FORCE_AWAKE=1` for continuous updates

### Movement Detection Not Working

1. Enable serial debug to see movement deltas: `-DDEBUG_SERIAL=1`
2. Look for `[MOVEMENT]` lines showing delta values
3. Adjust threshold if needed: `-DMOVEMENT_THRESHOLD_MG=120`
4. Ensure operating mode is HYBRID (mode 2)

### USB Detection Flickering

1. Increase score threshold: `-DVBAT_SCORE_MAX=15` or higher
2. Increase charge/discharge thresholds:
   ```ini
   -DVBAT_T_CHARGE=10.0
   -DVBAT_T_DISCHARGE=4.0
   ```
3. Note: USB detection is informational only (doesn't affect mode unless `DEV_MODE_ENABLE=1`)

## Development

### Enable DEV Mode

For rapid development with 211ms advertising intervals:

```ini
-DDEV_MODE_ENABLE=1
-DDEBUG_SERIAL=1
-DDEBUG_LCD=1
```

### Serial Output Example

```
=== Ruuvi DF5 Advertiser (Continuous Mode, Light Sleep) ===
Operating Mode: HYBRID
Hybrid Timing: FAST_INITIAL=60s, FAST_MOVEMENT=60s
Intervals: DEV=211ms, FAST=1285ms, SLOW=8995ms
BLE TX Power: 3dBm, Light Sleep: ENABLED
========================================================

[ADV] Mode=FAST interval=1285ms tx=3dBm uptime=5s seq=4 batt=4120mV
[SLEEP] Sleeping for 985ms until next advertisement
[MOVEMENT] Delta: dx=45 dy=245 dz=89 max=245 (threshold=120)
[MOVEMENT] Triggered FAST mode until uptime=65s (current=5s, duration=60s)
[STATUS] Mode=FAST [HYBRID] interval=1285ms uptime=10s seq=8 batt=4115mV USB=NO
```

## References

- [Ruuvi BLE Advertisements](https://docs.ruuvi.com/communication/bluetooth-advertisements)
- [Ruuvi Data Format 5 (RAWv2)](https://docs.ruuvi.com/communication/bluetooth-advertisements/data-format-5-rawv2)
- [Ruuvi Firmware 3.x](https://github.com/ruuvi/ruuvi.firmware.c)
- [M5StickC Plus2 Documentation](https://docs.m5stack.com/en/core/M5StickC%20PLUS2)
- [NTC Thermistor with Arduino/ESP32](https://github.com/e-tinkers/ntc-thermistor-with-arduino-and-esp32)

## License

This project is provided as-is for educational and personal use.

## Acknowledgments

- Ruuvi Innovations for the RuuviTag specification
- M5Stack for the M5StickC Plus2 and ENV III sensor
- ESP-IDF and Arduino-ESP32 communities
