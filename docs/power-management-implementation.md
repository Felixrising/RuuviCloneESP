# Power Management Implementation - Continuous Advertising (No Deep Sleep)

## Summary

This implementation uses Ruuvi's continuous advertising behavior without deep sleep. The M5StickC Plus2's RTC wake from deep sleep is unreliable, so we stay awake and use other power management features: reduced CPU frequency, disabled WiFi, LCD control, and adaptive BLE TX power.

## Ruuvi Reference Behavior

From [Ruuvi's official documentation](https://docs.ruuvi.com/communication/bluetooth-advertisements):

| Mode | Advertising Interval | Use Case |
|------|---------------------|----------|
| Test/Dev | 211ms ± 0-10ms | Development, rapid testing |
| Default/Fast | 1285ms ± 0-10ms | Normal operation, balanced power/responsiveness |
| Long-life/Slow | 8995ms ± 0-10ms | Extended battery life |

**Key insight:** Ruuvi uses **continuous advertising** at these intervals, not burst+sleep patterns.

## Our Implementation Strategy

### Continuous Advertising Pattern

Device stays awake continuously and advertises at mode-appropriate intervals:

```
┌────────────────────────────────────────────────────┐
│ [Wait] → Read Sensors → Advertise → [Repeat...]   │
└────────────────────────────────────────────────────┘
     ↑                    ↑
     └─ Interval delay   └─ Brief burst (300ms)
```

### Timing Configuration

```cpp
// Advertising intervals (Ruuvi-aligned)
DEV mode:  211ms   - Development, rapid updates
FAST mode: 1285ms  - Normal operation, discovery
SLOW mode: 8995ms  - Battery saving, long-term monitoring

// Advertisement burst duration
ADV_BURST_MS = 300ms  // How long to transmit per cycle
```

### Mode Selection Logic

**DEV Mode:**
- Trigger: USB connected OR `DEBUG_LCD_FORCE_AWAKE=1`
- Behavior: Stays awake, advertises every ~211ms
- LCD: Updates every 1 second
- Use: Development, debugging

**FAST Mode:**
- Trigger: Battery + (uptime < 60s OR movement in last 60s)
- Behavior: Wake every ~1.3s, advertise once, sleep
- LCD: Updates once per wake (if `DEBUG_LCD=1`)
- Use: Initial discovery, post-movement tracking

**SLOW Mode:**
- Trigger: Battery + uptime >= 60s + no recent movement
- Behavior: Wake every ~9s, advertise once, sleep
- LCD: Updates once per wake (if `DEBUG_LCD=1`)
- Use: Long-term monitoring, battery conservation

## Power Savings Features

### 1. Reduced CPU Frequency
```cpp
setCpuFrequencyMhz(80);  // Down from 240MHz
```
~66% power reduction from default 240MHz.

### 2. Conditional Peripherals
- **LCD:** Off in battery modes (unless `DEBUG_LCD=1`)
- **WiFi:** Always disabled
- **Serial:** Only if `DEBUG_SERIAL=1`

### 3. Fixed BLE TX Power
```cpp
BLE_TX_POWER = ESP_PWR_LVL_P3  // +3dBm
```
Balanced range (~15m) with lower power consumption than max (+9dBm).

### 4. Interval-Based Advertising
- **DEV:** 211ms - Rapid updates when needed
- **FAST:** 1.285s - Balanced responsiveness
- **SLOW:** 8.995s - Minimal radio usage

### 5. No Deep Sleep (M5StickC Plus2 Limitation)
Deep sleep wake is unreliable on this board. Staying awake with reduced CPU frequency and long SLOW intervals provides acceptable battery life for typical monitoring use.

## Battery Detection

### Filtered Voltage-Trend State Machine

Uses EWMA (Exponentially Weighted Moving Average) to detect USB connection:

```cpp
// Filters
Voltage: gVf = gVf + α(batt_mv - gVf)  // α = 0.1
Slope:   gSf = gSf + β(slope - gSf)    // β = 0.08

// State scoring
if (slope > +5.0 mV/min)   → score += 2  (charging)
if (slope < -2.0 mV/min)   → score -= 1  (discharging)
else                       → score → 0   (decay)

// State transition
if (score >= +8)  → USB connected
if (score <= -8)  → Battery only
```

**Advantages:**
- Immune to voltage jitter
- Rejects load spikes (>20mV ignored)
- Hysteresis prevents oscillation

**Trade-off:**
- 8+ cycles to latch (~10-70s depending on mode)

## Expected Power Consumption

### Estimated Current Draw (No Deep Sleep)

| Mode | Average Current | Battery Life (200mAh) |
|------|----------------|----------------------|
| DEV | ~35-45mA | ~4-6 hours |
| FAST | ~25-35mA | ~6-8 hours |
| SLOW | ~15-25mA | ~8-13 hours |

*Estimates for continuous operation at 80MHz CPU, +3dBm TX power. Actual values depend on sensor activity, LCD usage, and temperature.*

### Power Breakdown

| Component | Current Draw | Notes |
|-----------|-------------|-------|
| ESP32 @ 80MHz | ~15-20mA | Base system + BLE stack |
| BLE TX @ +3dBm | ~10-15mA peak | During advertising burst |
| Sensors (ENV III) | ~2-5mA | SHT30 + QMP6988 active |
| LCD (if enabled) | ~5-10mA | Backlight + display driver |
| USB detection | ~1-2mA | Voltage monitoring overhead |

### Trade-offs vs Deep Sleep

**Without deep sleep:**
- ✅ Reliable, predictable operation
- ✅ Fast LCD updates for debugging
- ✅ Consistent timing, no wake issues
- ❌ ~10-20x higher power consumption
- ❌ Shorter battery life (~8-13h vs ~100h)

**Practical use:** Best for applications with frequent USB charging or where reliability is more important than multi-day battery life.

## LCD Debug Display

When `DEBUG_LCD=1`:

```
┌────────────────────┐
│ USB:N SER:N        │  ← USB/Serial state
│ MODE:FAST          │  ← Current mode
│ FAST: 45s          │  ← Countdown to SLOW
│                    │
│ BATT:4125mV        │  ← Battery voltage
│ LVL : 85%          │  ← Battery level
└────────────────────┘
```

Updates:
- **DEV mode:** Every 1 second
- **FAST/SLOW mode:** Once per wake (~1-9s)
- **With `DEBUG_LCD_FORCE_AWAKE=1`:** Every 1s in all modes

## Build Flags

```ini
# Essential
-DBOARD_PROFILE=1              # M5StickC Plus2
-DSENSOR_PROFILE=2             # ENV III sensor

# Debug (disable for production)
-DDEBUG_LCD=1                  # Enable LCD display
-DDEBUG_SERIAL=1               # Enable serial logging
-DDEBUG_LCD_FORCE_AWAKE=1      # Stay awake for LCD debug

# Power (optional overrides)
-DUSB_MODE_OVERRIDE=1          # Force DEV mode
-DWAKE_PULSE_MS=1000           # LED pulse on wake (0 to disable)

# Timing (optional overrides)
-DFAST_ADV_MS=1285             # FAST advertisement interval
-DSLOW_ADV_MS=8995             # SLOW advertisement interval
-DWAKE_ACTIVE_MS=500           # Active time per wake
```

## Testing Checklist

### 1. Deep Sleep Wake
- [ ] Flash firmware
- [ ] Unplug USB
- [ ] Observe LED pulse every ~1-9s (indicates wake)
- [ ] Check BLE scanner for sequence number increments

### 2. USB Detection
- [ ] Start on battery (unplugged)
- [ ] Wait for Mode: FAST/SLOW on LCD
- [ ] Plug in USB
- [ ] Verify Mode: DEV within 30-60s
- [ ] Unplug USB
- [ ] Verify Mode: FAST/SLOW within 30-60s

### 3. Movement Detection
- [ ] Start on battery, wait >60s (SLOW mode)
- [ ] Shake device vigorously
- [ ] Verify Mode: FAST
- [ ] Wait 60s without movement
- [ ] Verify Mode: SLOW

### 4. Power Consumption
- [ ] Measure current in each mode with multimeter
- [ ] Verify deep sleep is working (should see periodic spikes, not constant draw)

## Known Issues & Limitations

### 1. M5StickC Plus2 Quirks
- **HOLD Pin (GPIO4):** Must be set HIGH early and held via `rtc_gpio_hold_en()`
- **Charging Detection Unreliable:** Do not trust `M5.Power.isCharging()` — use voltage-trend instead
- **RTC Wake:** Requires `gpio_deep_sleep_hold_en()` before sleep

### 2. LCD in Battery Mode
- Only updates once per wake
- Appears "frozen" between wakes (normal behavior)
- Use `DEBUG_LCD_FORCE_AWAKE=1` for continuous updates (testing only)

### 3. USB Detection Latency
- Takes 8+ cycles to latch state (10-70s)
- Initial boot guess may be wrong
- Fast transitions (<20s) may be missed

## Future Optimizations

### Option C: True Ruuvi Implementation
If we gain access to actual Ruuvi source code, we could implement:
- Light sleep instead of deep sleep (faster wake, slightly higher power)
- RTC-based scheduling (more precise timing)
- Sensor-specific power-down sequences
- Dynamic TX power adjustment

### Voltage Detection Improvements
```cpp
// Fast boot detection (immediate mode selection)
if (batt_mv > 4100) → assume USB
if (batt_mv < 3900) → assume battery
else                → use filtered state machine
```

### Adaptive Sleep
```cpp
// Adjust sleep based on recent miss rate
if (movement_detected_recently) → shorter sleep
if (stable_for_long_time)       → longer sleep
```

## References

- [Ruuvi BLE Advertisements](https://docs.ruuvi.com/communication/bluetooth-advertisements)
- [Ruuvi 3.x Heartbeat](https://docs.ruuvi.com/ruuvi-firmware/3.x/3.x-heartbeat)
- [Ruuvi 3.x Sensors](https://docs.ruuvi.com/ruuvi-firmware/3.x/3.x-sensors)
- [Ruuvi Firmware v3.34.1](https://github.com/ruuvi/ruuvi.firmware.c/tree/v3.34.1)
- [M5StickC Plus2 Docs](https://docs.m5stack.com/en/core/M5StickC%20PLUS2)
