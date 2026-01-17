# Branch Structure

## `main` (Production Branch)
**Status**: ✅ **Production Ready**

Clean, stable branch for RuuviTag DF5 (Data Format 5) advertising.

### Features
- RuuviTag Data Format 5 (DF5) BLE advertising
- Temperature, humidity, pressure, acceleration, battery voltage
- Configurable advertising intervals (FAST/SLOW/HYBRID modes)
- Multiple board support (M5StickC Plus2, XIAO ESP32S3, custom boards)
- NTC thermistor support
- M5 ENV III sensor support
- Power state detection (USB vs battery)
- Movement detection
- Compatible with Ruuvi Station app for **real-time sensor readings**

### What's NOT in Main
- ❌ No history logging
- ❌ No GATT server
- ❌ No NUS (Nordic UART Service)
- ❌ No Device Information Service

This branch is for **broadcast-only** operation - the device advertises sensor data that Ruuvi Station and other scanners can pick up without connecting.

---

## `feature/history-logging-wip` (Experimental Branch)
**Status**: ⚠️ **NOT WORKING - DO NOT USE**

Experimental implementation of RuuviTag history logging via Bluetooth GATT/NUS.

### What's Added
- History logging to LittleFS (circular buffer, 10 days @ 5min intervals)
- Nordic UART Service (NUS) GATT server
- Device Information Service (DIS)
- Ruuvi protocol message handling (11-byte format)
- Custom LittleFS partition (896KB)
- Comprehensive documentation and reference code

### Why It Doesn't Work
The Ruuvi Station app connects but immediately disconnects (reason 531) during GATT service discovery. Despite extensive debugging and multiple fixes, the root cause remains unknown. See `HISTORY_LOGGING_STATUS.md` in that branch for full details.

### When to Use This Branch
- **Never** for production
- Only if continuing development/debugging of history logging
- If you have access to BLE sniffers, actual RuuviTag hardware, or proprietary Ruuvi documentation

---

## How to Switch Branches

### To Production (Main)
```bash
git checkout main
pio run -t upload -e m5stickcplus2
```

### To Experimental (History Logging)
```bash
git checkout feature/history-logging-wip
pio run -t upload -e m5stickcplus2
```

**Note**: Always run `pio run -t upload` after switching branches to ensure the correct firmware is flashed.

---

## Remote Repository
- **GitHub**: https://github.com/Felixrising/RuuviCloneESP.git
- **Main branch** is pushed and kept in sync
- **Feature branch** can be pushed for archival/reference but should be marked as experimental

---

**Created**: January 17, 2026  
**Maintainer**: Split branches to keep main clean and production-ready
