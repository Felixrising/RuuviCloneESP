# Ruuvi Protocol - DECODED

## Source: Official ruuvi.firmware.c

From `ruuvi.endpoints.c/src/ruuvi_endpoints.h` (lines 55-238)

## Complete Message Structure

### Standard Message Format (11 bytes)

```c
[0]:    Destination endpoint (1 byte)
[1]:    Source endpoint (1 byte)  
[2]:    Operation/Type (1 byte)
[3-10]: Payload (8 bytes)
```

**Total: 11 bytes**

### Operation Types

| Operation | Hex | Description |
|-----------|-----|-------------|
| **Log Value Write** | `0x10` | Write log data |
| **Log Value Read** | `0x11` | Read log data (0x10 \| 0x01) |
| Value Write | `0x08` | Write sensor value |
| Value Read | `0x09` | Read sensor value (0x08 \| 0x01) |
| Sensor Config Write | `0x02` | Write sensor configuration |
| Sensor Config Read | `0x03` | Read sensor configuration |
| Log Config Write | `0x06` | Write log configuration |
| Log Config Read | `0x07` | Read log configuration |

**Pattern:** If operation is **even** → Write, if **odd** → Read (bit 0 set)

### Endpoints (Destinations)

| Endpoint | Hex | Description |
|----------|-----|-------------|
| **Temperature** | `0x30` | Temperature sensor |
| **Humidity** | `0x31` | Humidity sensor |
| **Pressure** | `0x32` | Pressure sensor |
| Environmental (All) | `0x3A` | Temp + Humidity + Pressure combined |
| Acceleration X | `0x40` | X-axis acceleration |
| Acceleration Y | `0x41` | Y-axis acceleration |
| Acceleration Z | `0x42` | Z-axis acceleration |
| Acceleration XYZ | `0x4A` | All axes combined |
| ADC Battery | `0x20` | Battery voltage |
| RTC | `0x21` | Real-time clock |
| Password | `0x2A` | Password/authentication |
| System Config | `0x22` | System configuration |

## Protocol Capture Analysis SOLVED!

### From our log sample:

```
Command:  3A 3A 11 5D C1 4C 56 00 00 00 00
Response: 3A 30 10 5D C1 34 62 00 00 08 5B
```

### Decoded Command:

```
[0]:    0x3A = Destination: RE_STANDARD_DESTINATION_ENVIRONMENTAL
[1]:    0x3A = Source: RE_STANDARD_DESTINATION_ENVIRONMENTAL (echo)
[2]:    0x11 = Operation: RE_STANDARD_LOG_VALUE_READ
[3-6]:  5D C1 4C 56 = Current time (big-endian, 1573790806 = 2019-11-15)
[7-10]: 00 00 00 00 = Start time (0 = from beginning)
```

**Translation:** "Read all log values from Environmental endpoint starting from timestamp 0"

### Decoded Response:

```
[0]:    0x3A = Destination: Environmental endpoint
[1]:    0x30 = Source: Temperature endpoint  
[2]:    0x10 = Operation: RE_STANDARD_LOG_VALUE_WRITE (response)
[3-6]:  5D C1 34 62 = Timestamp (big-endian, 1573784674)
[7-10]: 00 00 08 5B = Temperature value (encoded)
```

**Translation:** "Log entry from Temperature, timestamp 1573784674, value [encoded]"

## Scaling Factors

From `ruuvi_endpoints.h` lines 150-157:

```c
#define RE_STANDARD_TEMPERATURE_SF    (100.0F)  // centi-°C (divide by 100)
#define RE_STANDARD_HUMIDITY_SF       (100.0F)  // centi-RH% (divide by 100)
#define RE_STANDARD_PRESSURE_SF       (1.0F)    // Pa (no scaling)
#define RE_STANDARD_ACCELERATION_SF   (1000.0F) // milli-mg (divide by 1000)
#define RE_STANDARD_VOLTAGE_SF        (1000.0F) // milli-V (divide by 1000)
```

### Encoding Values

**Temperature:**
- Float to int32: `temp_int = (int32_t)(temp_celsius * 100.0)`
- Example: 22.50°C → 2250
- Bytes: big-endian int32

**Humidity:**
- Float to int32: `hum_int = (int32_t)(humidity_rh * 100.0)`
- Example: 45.20% → 4520
- Bytes: big-endian int32

**Pressure:**
- Float to int32: `pres_int = (int32_t)(pressure_pa * 1.0)`
- Example: 101325 Pa → 101325
- Bytes: big-endian int32

## Log Read Command Structure

From lines 99-106:

### Payload Layout (bytes 3-10):

```
[3]:  Current time MSB
[4]:  Current time B2
[5]:  Current time B3  
[6]:  Current time LSB
[7]:  Start time MSB
[8]:  Start time B2
[9]:  Start time B3
[10]: Start time LSB
```

**Current time:** Used by device to compensate for clock drift  
**Start time:** First timestamp to return (0 = all history)

## Log Write (Response) Structure

From lines 107-114:

### Payload Layout (bytes 3-10):

```
[3]:  Timestamp MSB
[4]:  Timestamp B2  
[5]:  Timestamp B3
[6]:  Timestamp LSB
[7]:  Value MSB
[8]:  Value B2
[9]:  Value B3
[10]: Value LSB
```

**Timestamp:** Big-endian uint32 (seconds since epoch)  
**Value:** Big-endian int32 (scaled by endpoint's scaling factor)

## Multi-Sensor Response Pattern

For environmental endpoint (0x3A), device sends **separate messages per sensor**:

```
1. [3A][30][10][timestamp][temp_value]    # Temperature
2. [3A][31][10][timestamp][hum_value]     # Humidity  
3. [3A][32][10][timestamp][pres_value]    # Pressure
```

This matches our observed protocol capture perfectly!

## Comparison: Our Implementation vs Official

| Aspect | Our Implementation | Official Protocol | Status |
|--------|-------------------|-------------------|--------|
| **Message Length** | Variable (wrong) | **Always 11 bytes** | ❌ Fix needed |
| **Header Prefix** | 0x3A 0x3A (treated as magic) | Dest=0x3A, Src=0x3A | ✅ Accidentally correct! |
| **Command Code** | 0x11 (correct) | 0x11 = LOG_VALUE_READ | ✅ Correct |
| **Response Format** | [3A][stream_id][10][ts][val]... | [dest][src][10][ts][val] | ✅ Correct |
| **Timestamp** | Big-endian uint32 | Big-endian uint32 | ✅ Correct |
| **Value Encoding** | Big-endian int16 (wrong!) | **Big-endian int32** | ❌ Fix needed |
| **Scaling** | DF5 format (wrong) | RE_STANDARD_*_SF | ❌ Fix needed |

## Critical Fixes Needed

### 1. Value Size: int32 not int16

**Current (wrong):**
```cpp
uint16_t value;  // 2 bytes
packet[7] = (value >> 8) & 0xFF;
packet[8] = value & 0xFF;
packet[9] = 0x00;
packet[10] = 0x00;
```

**Correct:**
```cpp
int32_t value;  // 4 bytes
packet[7] = (value >> 24) & 0xFF;
packet[8] = (value >> 16) & 0xFF;
packet[9] = (value >> 8) & 0xFF;
packet[10] = value & 0xFF;
```

### 2. Scaling Factors

**Current (DF5 encoding):**
- Temperature: `(int16_t)(temp_c * 200.0)` → 0.005°C steps
- Humidity: `(uint16_t)(humidity * 400.0)` → 0.0025% steps  
- Pressure: `(uint16_t)((pressure - 50000) / 1.0)` → 1 Pa steps

**Correct (Ruuvi Endpoints):**
- Temperature: `(int32_t)(temp_c * 100.0)` → 0.01°C steps
- Humidity: `(int32_t)(humidity * 100.0)` → 0.01% steps
- Pressure: `(int32_t)(pressure_pa * 1.0)` → 1 Pa steps (no offset!)

### 3. Separate Messages Per Sensor

**Current:** Single stream with changing stream IDs  
**Correct:** Each sensor is a separate message with its own destination

## Updated Protocol Flow

### Client Request:

```
Destination: 0x3A (Environmental)
Source:      0x3A (Environmental)  
Operation:   0x11 (LOG_VALUE_READ)
Payload:     [current_time:4][start_time:4]
```

### Device Response (per sample):

```
Message 1 (Temperature):
  [3A][30][10][timestamp:4][temp_int32:4]

Message 2 (Humidity):
  [3A][31][10][timestamp:4][hum_int32:4]

Message 3 (Pressure):
  [3A][32][10][timestamp:4][pres_int32:4]
```

## Implementation Checklist

- [ ] Change response value from uint16_t to int32_t (4 bytes)
- [ ] Update scaling factors to match RE_STANDARD_*_SF
- [ ] Ensure all messages are exactly 11 bytes
- [ ] Keep destination as 0x3A (environmental)
- [ ] Keep source as sensor endpoint (0x30, 0x31, 0x32)
- [ ] Keep operation as 0x10 (LOG_VALUE_WRITE) for responses
- [ ] Use big-endian for all multi-byte values
- [ ] Test with Ruuvi Station to verify

## References

- `ruuvi.firmware.c/src/ruuvi.endpoints.c/src/ruuvi_endpoints.h`
- Official docs: https://docs.ruuvi.com/communication/bluetooth-connection/nordic-uart-service-nus
- Repository: https://github.com/ruuvi/ruuvi.firmware.c

## Next Steps

1. **Update `src/history/history_nus.h`** with correct value encoding
2. **Change packet format** to use int32 for values (bytes 7-10)
3. **Update scaling factors** to match official specification
4. **Test** with Ruuvi Station app
5. **Verify** response format matches exactly

The mystery is solved! Our protocol was *almost* correct but had the wrong value size and scaling. Easy fix! 🎉
