# History Logging Feature Status

## ⚠️ **STATUS: NOT WORKING - WORK IN PROGRESS** ⚠️

This branch contains an **incomplete implementation** of RuuviTag-compatible history logging via Nordic UART Service (NUS). The device connects but immediately disconnects during GATT service discovery.

---

## What's Implemented

### ✅ Core Infrastructure
- **History Storage (`src/history/history_log.h`)**
  - Circular buffer implementation on LittleFS
  - Stores temperature, humidity, pressure, timestamp
  - Configurable retention (default: 10 days at 5-minute intervals = 2880 entries)
  - Custom partition for LittleFS (896KB in `partitions_custom.csv`)

- **Nordic UART Service (`src/history/history_nus.h`)**
  - NUS GATT service with RX/TX characteristics
  - Device Information Service (DIS) with manufacturer, model, serial, FW/HW versions
  - CCCD descriptor for TX notifications
  - Connection/disconnection callbacks
  - Ruuvi protocol message parser (11-byte fixed format)

- **Protocol Implementation**
  - Ruuvi Standard Message format: `[dest][src][op][payload:8]`
  - LOG_VALUE_READ (0x11) command handling
  - Correct value encoding: temperature (÷2), humidity (÷4), pressure (+50000)
  - Terminator packet (0xFF...FF) to signal end of history stream
  - Empty history ACK packet to prevent client timeout

### 📚 Documentation
- `docs/history-logging-feasibility.md` - Initial feasibility analysis
- `docs/history-logging-usage.md` - API reference and usage guide
- `docs/ruuvi-firmware-analysis.md` - Analysis of official Ruuvi firmware
- `docs/ruuvi-protocol-decoded.md` - Detailed NUS protocol specification

### 🔧 Configuration
- Build flags in `platformio.ini`:
  - `HISTORY_LOG_ENABLE` - Enable/disable history logging
  - `HISTORY_INTERVAL_SEC` - Logging interval (default: 300s = 5 min)
  - `HISTORY_MAX_DAYS` - Retention period (default: 10 days)
  - `HISTORY_FS_SIZE_KB` - LittleFS partition size (default: 896KB)

---

## ❌ What's NOT Working

### The Problem
**Ruuvi Station app connects but immediately disconnects with reason 531 (0x213)** during GATT service discovery.

### Symptoms Observed
```
[NUS] Client connected from xx:xx:xx:xx:xx:xx
[NUS] MTU: 23, Interval: 16, Latency: 0, Timeout: 500
[NUS] Client disconnected (reason: 531 = 0x213)
[NUS] → Possible GATT/service discovery issue
```

**No RX messages are ever received** - the client drops before sending any commands.

### Attempted Fixes (All Failed)
1. ✅ Added Device Information Service (DIS) with all required characteristics
2. ✅ Added explicit CCCD descriptor to TX characteristic
3. ✅ Shortened scan response to avoid UUID truncation (name: "Ruuvi", removed battery service data)
4. ✅ Advertised NUS (`6e400001...`), DIS (`0x180A`), and Ruuvi service (`0xFC98`) UUIDs
5. ✅ Called `server->start()` before advertising (was missing initially)
6. ✅ Stop advertising on connect, restart on disconnect
7. ✅ Skip advertising updates while NUS client connected
8. ✅ Added test data on boot for immediate testing

### Root Cause Hypothesis
The Ruuvi Station app expects:
- Specific GATT service/characteristic layout
- Possibly bonding/pairing requirements
- Specific connection parameters or timing
- Unknown proprietary extensions or characteristic properties

The disconnect happens **during service discovery**, which suggests the app scans the GATT table, doesn't find what it expects, and immediately drops the connection.

---

## Reference Materials Included

This branch includes cloned reference repositories in `references/` (gitignored):
- `ruuvi.firmware.c` - Official RuuviTag firmware (nRF52, C)
- `com.ruuvi.station` - Official Ruuvi Station Android app (Kotlin)
- `ruuvi.air.ble_nus` - Ruuvi Air NUS client example (Zephyr)

Key files analyzed:
- `ruuvi.firmware.c/src/app_log.c` - History logging implementation
- `ruuvi.firmware.c/src/app_comms.c` - NUS service setup
- `ruuvi.firmware.c/src/ruuvi.endpoints.c/` - Protocol message handling
- `com.ruuvi.station/.../BluetoothGattInteractor.kt` - App-side GATT handling

---

## Next Steps for Future Work

### Recommended Debugging Approach
1. **Use nRF Connect** to inspect GATT table and verify:
   - NUS service (`6e400001...`) is visible
   - RX characteristic (`6e400002...`) has WRITE | WRITE_NO_RESPONSE
   - TX characteristic (`6e400003...`) has NOTIFY + CCCD (0x2902)
   - DIS service (`0x180A`) has all characteristics (0x2A29, 0x2A24, 0x2A25, 0x2A26, 0x2A27)
   - Can manually enable TX notifications
   - Can manually write `3A 3A 11 ...` to RX and receive responses

2. **Compare with Real RuuviTag**
   - Capture GATT table from actual RuuviTag
   - Compare characteristic properties, descriptors, handles
   - Check for any extra services or characteristics we're missing

3. **BLE Sniffer**
   - Use Ellisys or Nordic Sniffer to capture connection sequence
   - Compare working RuuviTag connection vs our failed connection
   - Look for differences in pairing, MTU exchange, service discovery

### Possible Issues to Investigate
- **Connection parameters**: Ruuvi firmware requests specific intervals (turbo/standard/low-power)
- **Security requirements**: May need bonding or specific security level
- **Characteristic properties**: May be missing flags (e.g., READ on TX/RX)
- **Descriptor configuration**: CCCD may need specific initial value
- **Service order**: DIS/NUS/DFU may need to be in specific order
- **Advertising timing**: May need to advertise longer before accepting connection

---

## How to Test This Branch

### Build and Upload
```bash
pio run -t upload -e m5stickcplus2
pio device monitor
```

### Expected Boot Output
```
=== Ruuvi DF5 Advertiser v2024.01.17 WITH HISTORY ===
[HISTORY] Initialized: 3/2880 entries, 308-309
[DIS] Device Information Service initialized
[NUS] Service initialized
[BLE] Server started, ready for connections
```

### Try History Download
1. Open Ruuvi Station app
2. Add the device (will see broadcast data)
3. Tap History → Sync
4. **Expected**: Immediate disconnect with "Download failed" error
5. Check serial monitor for `[NUS]` messages

---

## Returning to Main Branch

To return to the stable main branch without history logging:

```bash
git checkout main
```

The main branch has been cleaned of all history logging code and is production-ready for RuuviTag data format 5 (DF5) broadcasting.

---

**Last Updated**: January 17, 2026  
**Branch Created**: During debugging session attempting Ruuvi Station app compatibility  
**Maintainer Note**: This feature may require access to actual RuuviTag hardware or proprietary documentation to complete successfully.
