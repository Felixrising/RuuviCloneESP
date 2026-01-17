# Operating Mode Configuration

## Overview

The firmware supports three simple operating modes:

1. **FAST_ONLY** - Always advertise at 1285ms intervals (maximum responsiveness)
2. **SLOW_ONLY** - Always advertise at 8995ms intervals (maximum battery life)
3. **HYBRID** - Smart switching between FAST and SLOW based on boot time and movement (balanced)

In HYBRID mode, two timing parameters control transitions:
- **FAST_MODE_INITIAL_MS** - How long to stay in FAST mode after boot
- **FAST_MODE_MOVEMENT_MS** - How long to stay in FAST mode after movement detected

## Operating Modes

### MODE 0: FAST_ONLY
**Best for:** Maximum responsiveness, always-available

```ini
-DOPERATING_MODE=0
```

**Behavior:**
- Always advertises at 1285ms intervals
- No mode switching
- Immediate discovery and updates
- Movement detection still tracked but doesn't affect mode

**Power:** ~5-8mA average  
**Battery Life:** ~25-40h (200mAh)  
**Use Cases:**
- Development and testing
- High-frequency monitoring
- Applications where battery isn't a concern
- Always need quick response times

---

### MODE 1: SLOW_ONLY
**Best for:** Maximum battery life, infrequent updates acceptable

```ini
-DOPERATING_MODE=1
```

**Behavior:**
- Always advertises at 8995ms intervals (~9 seconds)
- No mode switching
- Consistent low-power operation
- Movement detection still tracked but doesn't affect mode

**Power:** ~2-3mA average  
**Battery Life:** ~65-100h (200mAh)  
**Use Cases:**
- Long-term environmental monitoring
- Stationary sensors
- Battery-constrained deployments
- Infrequent data updates acceptable

---

### MODE 2: HYBRID (Default) ✓
**Best for:** Balanced responsiveness and battery life

```ini
-DOPERATING_MODE=2
-DFAST_MODE_INITIAL_MS=60000   ; 60s
-DFAST_MODE_MOVEMENT_MS=60000  ; 60s
```

**Behavior:**
- Boot → FAST mode (1285ms) for initial discovery
- After FAST_MODE_INITIAL_MS → SLOW mode (8995ms)
- Movement detected → FAST mode for FAST_MODE_MOVEMENT_MS
- No movement → back to SLOW mode

**Power:** ~3-6mA average (depends on activity)  
**Battery Life:** ~35-65h (200mAh)  
**Use Cases:**
- General purpose monitoring
- Mobile/portable sensors
- Activity-triggered recording
- Balance between discovery speed and battery life

---

## Mode Behavior Reference

| Mode | Advertising Interval | Use Case | Power Draw |
|------|---------------------|----------|------------|
| **DEV** | 211ms | Development/testing | ~35-45mA |
| **FAST** | 1285ms | Discovery, activity | ~5-8mA |
| **SLOW** | 8995ms | Long-term monitoring | ~2-3mA |

## How to Select a Mode

1. **Edit `platformio.ini`** in your project root
2. **Uncomment your desired mode:**
   ```ini
   -DOPERATING_MODE=0  ; FAST_ONLY
   ; OR
   -DOPERATING_MODE=1  ; SLOW_ONLY
   ; OR
   -DOPERATING_MODE=2  ; HYBRID (default)
   ```
3. **If using HYBRID, adjust timing (optional):**
   ```ini
   -DFAST_MODE_INITIAL_MS=60000   ; 60s after boot
   -DFAST_MODE_MOVEMENT_MS=60000  ; 60s after movement
   ```
4. **Rebuild and flash:**
   ```
   pio run -t upload -e m5stickcplus2
   ```

## HYBRID Mode Custom Timing

Valid range: 5000 to 300000 (5s to 5 minutes)

**Examples:**
```ini
; Quick transitions (battery priority)
-DFAST_MODE_INITIAL_MS=30000   ; 30s after boot
-DFAST_MODE_MOVEMENT_MS=10000  ; 10s after movement

; Extended monitoring (responsiveness priority)
-DFAST_MODE_INITIAL_MS=120000  ; 120s after boot
-DFAST_MODE_MOVEMENT_MS=120000 ; 120s after movement
```

## Serial Monitor Output

When `DEBUG_SERIAL=1`, you'll see configuration at boot:

**FAST_ONLY or SLOW_ONLY:**
```
=== Ruuvi DF5 Advertiser (Continuous Mode, Light Sleep) ===
Operating Mode: FAST_ONLY
Intervals: DEV=211ms, FAST=1285ms, SLOW=8995ms
BLE TX Power: 3dBm, Light Sleep: ENABLED
========================================================
```

**HYBRID:**
```
=== Ruuvi DF5 Advertiser (Continuous Mode, Light Sleep) ===
Operating Mode: HYBRID
Hybrid Timing: FAST_INITIAL=60s, FAST_MOVEMENT=60s
Intervals: DEV=211ms, FAST=1285ms, SLOW=8995ms
BLE TX Power: 3dBm, Light Sleep: ENABLED
========================================================
```

**Periodic status updates:**
```
[STATUS] Mode=FAST (DEV_DISABLED) [HYBRID] interval=1285ms uptime=45s seq=35 batt=3920mV USB=NO
[HYBRID] fast_until=15s, FAST_INITIAL=60s, FAST_MOVEMENT=60s
```

## Movement Detection

Movement threshold: **120mg** (milligravity)

- Detects actual physical movement (shaking, tilting, dropping)
- Ignores micro-vibrations and sensor noise
- First reading doesn't trigger false positive
- Debug output shows delta when movement detected:
  ```
  [MOVEMENT] Delta: dx=45 dy=245 dz=89 max=245 (threshold=120)
  ```

## Mode Transition Logic (HYBRID Mode Only)

```
Boot
  ↓
FAST mode (1285ms intervals)
  ↓ (after FAST_MODE_INITIAL_MS with no movement)
SLOW mode (8995ms intervals)
  ↓ (movement detected)
FAST mode (1285ms intervals)
  ↓ (after FAST_MODE_MOVEMENT_MS with no movement)
SLOW mode (8995ms intervals)
```

**Timer Extension:** If movement is detected during a FAST period, the timer resets:
- Boot at t=0s
- Initial FAST period: 0s → 60s (FAST_MODE_INITIAL_MS)
- Movement at t=30s
- Extended FAST period: now until t=90s (60s from movement)
- Movement at t=85s
- Extended again: now until t=145s (60s from last movement)

**Fixed Modes (FAST_ONLY / SLOW_ONLY):**
- No transitions, mode stays constant
- Movement counter still increments but doesn't affect intervals

## Recommendations by Use Case

| Use Case | Recommended Mode | Timing (if HYBRID) | Reason |
|----------|------------------|-------------------|--------|
| **Development/Testing** | FAST_ONLY | N/A | Need continuous rapid updates |
| **Home monitoring (stationary)** | SLOW_ONLY or HYBRID | 30s/10s | Rarely moves, prioritize battery |
| **Pet/vehicle tracking** | HYBRID | 120s/120s | Frequent movement, need responsiveness |
| **General purpose** | HYBRID (default) | 60s/60s | Good balance |
| **Remote deployment** | SLOW_ONLY | N/A | Maximize time between battery changes |
| **Activity logging** | HYBRID | 60s/60s | Capture movement events |

## Power Consumption Summary

Based on 200mAh battery with ENV III sensor (light sleep enabled, +3dBm TX, 80MHz CPU):

| Mode | Average Current | Battery Life (200mAh) | Activity Impact |
|------|----------------|----------------------|-----------------|
| **FAST_ONLY** | ~5-8mA | ~25-40h | N/A |
| **SLOW_ONLY** | ~2-3mA | ~65-100h | N/A |
| **HYBRID** (low activity) | ~3-4mA | ~50-65h | 10% FAST |
| **HYBRID** (moderate) | ~4-5mA | ~40-50h | 30% FAST |
| **HYBRID** (high activity) | ~5-7mA | ~30-40h | 60% FAST |
