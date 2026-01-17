# RuuviTag History Logging Feasibility Analysis

## Overview

RuuviTag firmware v3.30.0+ supports storing up to 10 days of sensor measurements in internal flash memory and downloading them via Bluetooth using the Nordic UART Service (NUS).

## Protocol Analysis

### BLE Services Used

Based on the connection log sample:

- **Service UUID:** `6e400001-b5a3-f393-e0a9-e50e24dcca9e` (Nordic UART Service)
- **RX Characteristic (Write):** `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- **TX Characteristic (Notify):** `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

### Connection Flow

1. **Initial Connection:** Client connects to device and discovers services
2. **Enable Notifications:** Client enables notifications on TX characteristic
3. **Real-time Advertisements:** Device continuously sends DF5 payloads (format 0x05) via notifications
4. **History Request:** Client writes command to RX characteristic
5. **History Response:** Device streams historical data via TX notifications

### Command Structure

From log sample, the history read command appears to be:
```
3A-3A-11-5D-C1-4C-56-00-00-00-00  (11 bytes)
```

Breakdown (hypothesis):
- Bytes 0-1: `3A-3A` - Command header/magic bytes
- Byte 2: `11` - Command type (0x11 = read history?)
- Bytes 3-6: `5D-C1-4C-56` - Timestamp (seconds since epoch?)
- Bytes 7-10: `00-00-00-00` - Parameters (start offset, count?)

### Response Format

Historical data is returned in chunks prefixed with:
- `3A-30-...` - Temperature data?
- `3A-31-...` - Humidity data?
- `3A-32-...` - Pressure data?

Each chunk appears to be 11 bytes:
```
3A-30-10-5D-C1-34-62-00-00-08-5B
│  │  │  └────┬────┘ └──┬──┘ └─┬─┘
│  │  │       │         │      │
│  │  │       │         │      └─ Checksum?
│  │  │       │         └──────── Sensor value
│  │  │       └────────────────── Timestamp
│  │  └────────────────────────── Data format
│  └───────────────────────────── Data stream ID (0=temp, 1=hum, 2=press)
└──────────────────────────────── Header byte
```

## Storage Requirements

### Memory Calculation

For 10 days of 5-minute interval logging:
- **Samples per day:** 288 (24 hours × 60 min / 5 min)
- **Total samples:** 2,880 (288 × 10 days)
- **Sensors:** 3 (temperature, humidity, pressure)
- **Total entries:** 8,640 (2,880 × 3)

**Per-entry storage:**
- Timestamp: 4 bytes (Unix epoch)
- Temperature: 2 bytes (int16_t)
- Humidity: 2 bytes (uint16_t)
- Pressure: 2 bytes (uint16_t)
- **Total per sample:** 10 bytes

**Total storage needed:**
- 10 bytes × 2,880 samples = **28.8 KB**
- Add ~10% for metadata, indexes, wear leveling = **~32 KB**

### ESP32 Flash Availability

**M5StickC Plus2 (ESP32-S3):**
- Total flash: 4 MB
- Firmware: ~1-1.5 MB
- File system (LittleFS): Can allocate 512 KB - 1 MB
- **Available for history:** 100-500 KB ✅

**Generic ESP32-S3:**
- Similar availability
- Easily allocate 64-128 KB for history logging ✅

## Implementation Challenges

### 1. Flash Wear Leveling ⚠️
**Problem:** Writing every 5 minutes = 288 writes/day
- Over 10 days = 2,880 writes
- Over 1 year = 105,120 writes to same flash sector

**ESP32 Flash Endurance:**
- Typical: 10,000-100,000 erase cycles per sector
- With 288 writes/day, a single 4KB sector would wear out in ~35-350 days

**Solution:**
- Use **circular buffer** with multiple sectors
- Implement **LittleFS** (already has wear leveling)
- Allocate 64 KB (16 sectors × 4 KB) for ~180 days of wear leveling
- Or use **NVS (Non-Volatile Storage)** with built-in wear leveling

### 2. Power Consumption 🔋
**Additional Power Costs:**
- Flash write: ~20 mA for ~5-10 ms = ~0.03 mAh per write
- 288 writes/day = ~8.6 mAh/day additional
- Impact: ~3-10% battery life reduction

**Mitigation:**
- Write during wake cycles (already awake for sensor read)
- Minimal additional power impact if batched with sensor reading

### 3. NimBLE GATT Service Implementation 🛠️
**Required:**
- Add Nordic UART Service (NUS) UUIDs
- Implement characteristic callbacks for RX (commands) and TX (responses)
- Handle connection state (stop advertising while connected)
- Implement command parser for history requests
- Implement streaming response chunking

**NimBLE Support:**
- ✅ Full GATT server support
- ✅ Custom services and characteristics
- ✅ Notification support
- ✅ Connection callbacks

**Complexity:** Medium - requires ~500-1000 lines of code

### 4. Protocol Reverse Engineering 🔍
**Challenge:** Ruuvi's history protocol is not fully documented

**Approach:**
1. Analyze provided log sample for patterns
2. Test with Ruuvi Station mobile app
3. Use nRF Connect to intercept and decode
4. Implement minimal subset (e.g., "download all" only)

**Risk:** Incomplete or incorrect protocol = incompatibility with official apps

### 5. Time Synchronization ⏰
**Problem:** ESP32 has no RTC battery backup
- Loses time on power cycle
- Need to sync time via NTP or BLE connection

**Solutions:**
- **NTP sync** at boot (requires WiFi - not ideal for battery)
- **BLE time sync** from connected device
- **Relative timestamps** (seconds since boot) - requires app support

### 6. Data Integrity 💾
**Concerns:**
- Power loss during write
- Flash corruption
- Checksum validation

**Solutions:**
- Use LittleFS (atomic writes, checksums)
- Write timestamp first, then data
- Mark incomplete entries as invalid

## Feasibility Assessment

| Aspect | Feasibility | Effort | Notes |
|--------|-------------|--------|-------|
| **Storage Space** | ✅ High | Low | 32 KB easily available |
| **Flash Wear** | ⚠️ Medium | Medium | Need proper wear leveling strategy |
| **Power Impact** | ✅ High | Low | <10% battery reduction |
| **GATT/NUS Implementation** | ✅ High | Medium-High | NimBLE supports all required features |
| **Protocol Compatibility** | ⚠️ Medium | High | Protocol not fully documented |
| **Time Sync** | ⚠️ Medium | Medium | RTC sync needed |
| **Data Integrity** | ✅ High | Medium | LittleFS provides safeguards |

**Overall Feasibility: ✅ YES, but with caveats**

## Recommended Implementation Approach

### Phase 1: Basic Logging (No BLE Download)
**Goal:** Store 10 days of history locally

1. Allocate 64 KB LittleFS partition
2. Implement circular buffer with timestamps
3. Write sensor data every 5 minutes
4. Test flash wear over 30 days
5. Add serial command to dump history

**Effort:** 2-3 days
**Value:** Enables local data recovery, debugging

### Phase 2: NUS Service Implementation
**Goal:** Enable BLE connection and service discovery

1. Add Nordic UART Service to NimBLE GATT server
2. Implement RX/TX characteristics
3. Handle connection state (pause advertising)
4. Test with nRF Connect app
5. Implement basic echo/ping command

**Effort:** 3-4 days
**Value:** Foundation for any BLE commands

### Phase 3: History Download Protocol
**Goal:** Download history via BLE

1. Decode Ruuvi history request format
2. Implement command parser
3. Implement response chunking and streaming
4. Add timestamp parameters (start/end)
5. Test with sample requests from log file
6. Test with Ruuvi Station app (if compatible)

**Effort:** 5-7 days
**Value:** Full history download feature

### Phase 4: Time Synchronization
**Goal:** Accurate timestamps

1. Implement BLE time sync command
2. Store RTC offset in NVS
3. Validate timestamps after power cycle
4. Optional: NTP sync on WiFi connect

**Effort:** 2-3 days
**Value:** Usable timestamps for history

**Total Effort:** 12-17 days of development

## Alternative: Simpler Approach

If full RuuviTag compatibility is not required:

### Custom History Protocol
- Use simpler JSON or binary format
- Custom mobile app or Python script to download
- Skip complex timestamp encoding
- Much faster implementation: ~3-5 days

### Cloud Upload Option
- Enable WiFi periodically (e.g., once per hour)
- Upload history to MQTT/HTTP endpoint
- No BLE download needed
- Easier for long-term storage and analysis

## Recommendation

**For this project:**

1. **Implement Phase 1 (Basic Logging)** first
   - Valuable for debugging and diagnostics
   - Foundation for future features
   - Low risk, high value

2. **Defer Phase 2-4** until:
   - Core emulation is stable and tested with Venus OS
   - User demand for history download feature
   - Time to properly reverse engineer and test protocol

3. **Consider custom protocol** instead of Ruuvi compatibility
   - Faster implementation
   - More flexibility
   - Document protocol for community use

## Conclusion

**Is it feasible?** YES ✅

**Should we implement it now?** PROBABLY NOT 🤔

**Reasoning:**
- Core RuuviTag emulation (DF5 advertising) is working well
- History logging adds significant complexity
- Protocol reverse engineering is time-consuming
- Value is unclear for Venus OS use case (which reads real-time data)
- Better to focus on stability, power management, and multiple sensor support

**When to implement:**
- If Venus OS adds history support
- If users request offline data logging
- If there's a specific use case requiring 10-day history
- After core firmware is stable and well-tested

## References

- [Ruuvi Log Read Documentation](https://docs.ruuvi.com/communication/bluetooth-connection/nordic-uart-service-nus/log-read)
- [Nordic UART Service Specification](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/libraries/bluetooth_services/services/nus.html)
- [ruuvitag-sensor Python Library](https://github.com/ttu/ruuvitag-sensor)
- Sample connection log: `references/v3_28_0_connection_logged history read sample.txt`
