# RuuviTag Official Firmware Analysis

## Source

Official Ruuvi firmware repository: https://github.com/ruuvi/ruuvi.firmware.c ([GitHub](https://github.com/ruuvi/ruuvi.firmware.c))

Key reference file: `application_service_if.c` from ruuvi_examples/ruuvi_firmware/ble_services/

## Critical Finding: NUS Message Protocol

### Official Ruuvi Protocol (from application_service_if.c)

```c
static void nus_data_handler(ble_nus_t * p_nus, uint8_t * p_data, uint16_t length)
{
  NRF_LOG_INFO("Received %s\r\n", (uint32_t)p_data);
  //Assume standard message - TODO: Switch by endpoint
  if(length == 11){
    ruuvi_standard_message_t message = { 
      .destination_endpoint = p_data[0],
      .source_endpoint = p_data[1],
      .type = p_data[2],
      .payload = {0}
    };
    memcpy(&(message.payload[0]), &(p_data[3]), sizeof(message.payload));
    //Schedule handling of the message - do not process in interrupt context
    app_sched_event_put(&message, sizeof(message), 
                        ble_gatt_scheduler_event_handler);
  }
}
```

### Message Structure

**Total: 11 bytes**
```
[0]:    destination_endpoint
[1]:    source_endpoint  
[2]:    type (command type)
[3-10]: payload (8 bytes)
```

### Key Differences from Our Implementation

| Aspect | Our Implementation | Official Ruuvi | Impact |
|--------|-------------------|----------------|---------|
| **Message Length** | Variable | **Always 11 bytes** | ❌ Incompatible |
| **Message Format** | `3A 3A [cmd] [params...]` | `[dest] [src] [type] [payload:8]` | ❌ Incompatible |
| **Endpoints** | Not used | destination/source endpoints | ❌ Missing |
| **Processing** | Immediate in callback | **Scheduled (app_sched_event_put)** | ⚠️ Different architecture |

## Ruuvi Endpoints System

From the code, they reference:
- `ruuvi_endpoints.h` - Defines endpoint addresses
- `ruuvi_standard_message_t` - Standard message structure
- `ble_gatt_scheduler_event_handler` - Async message handler

This suggests a **message routing system** where:
- Commands are addressed to specific endpoints (modules)
- Each endpoint handles different functionality
- Processing is asynchronous (scheduled, not in interrupt)

## Files to Review for Complete Understanding

### Essential Files

1. **`src/app_log.c`** - History logging implementation
   - How they store entries
   - Log format and structure
   - Read/write mechanisms

2. **`src/app_comms.c`** - Communication handling
   - BLE communication layer
   - How history is transmitted
   - Packet framing and chunking

3. **`src/app_dataformats.c`** - Data encoding
   - DF5 encoding specifics
   - Timestamp handling
   - Data compression

4. **`src/ruuvi_endpoints.h`** - Endpoint definitions
   - Available endpoints
   - Command types per endpoint
   - Message routing

5. **`ble_services/ble_bulk_transfer.h/.c`** - Bulk data transfer
   - How large datasets (history) are transferred
   - Flow control
   - Chunking strategy

### Configuration Files

6. **`src/bluetooth_application_config.h`** - BLE configuration
7. **`src/bluetooth_board_config.h`** - Board-specific BLE settings

## Our Current vs Ruuvi Protocol

### What We Implemented (Protocol Capture Based)

From the captured log sample, we reverse-engineered:

```
Command:  3A 3A 11 [timestamp:4] [params:4]
Response: 3A [stream_id] 10 [timestamp:4] [value:2] [checksum:2]
```

This appears to be a **higher-level application protocol** on top of NUS, not the raw NUS message format.

### Hypothesis: Multi-Layer Protocol

```
Layer 1 (NUS Transport): 11-byte messages [dest][src][type][payload:8]
Layer 2 (Application): Commands prefixed with 3A 3A, chunked into 11-byte NUS packets
Layer 3 (Data Stream): History data formatted with stream IDs (0x30, 0x31, 0x32)
```

Our implementation may be correct for **Layer 2/3** but missing **Layer 1** framing.

## Recommendations

### Option 1: Hybrid Approach (Current + Endpoints)

Keep our current implementation but add endpoint framing:

```c
// Wrap our commands in endpoint messages
void sendHistoryCommand(uint32_t timestamp) {
  uint8_t nus_msg[11] = {
    0x00,  // destination_endpoint (broadcast or specific endpoint)
    0x01,  // source_endpoint (client)
    0x11,  // type (read history command)
    // payload (8 bytes):
    0x3A, 0x3A,  // Application layer header
    (timestamp >> 24), (timestamp >> 16),
    (timestamp >> 8), timestamp,
    0x00, 0x00
  };
  // Send via NUS
}
```

**Pros:**
- Minimal changes to existing code
- Adds compatibility layer
- Can coexist with current implementation

**Cons:**
- Still might not match exact Ruuvi behavior
- Mixed protocol layers

### Option 2: Full Ruuvi Protocol Adoption

Clone `ruuvi.firmware.c` and study:
1. Extract `app_log.c`, `app_comms.c`, `ruuvi_endpoints.h`
2. Port to ESP32 (from Nordic nRF52)
3. Integrate with our sensor/board code

**Pros:**
- 100% compatible with Ruuvi Station
- Leverages tested, production code
- Community support

**Cons:**
- Significant refactoring required
- Nordic SDK dependencies to port
- More complex codebase

### Option 3: Wait and Test

Keep current implementation and test with Ruuvi Station:
- If it works → done!
- If not → analyze app traffic with nRF Connect
- Adjust based on actual behavior

**Pros:**
- No premature optimization
- Learn what actually works vs theory
- Incremental fixes

**Cons:**
- May discover incompatibilities late
- Could waste time on wrong approach

## Recommended Action Plan

### Phase 1: Test Current Implementation (NOW)

1. Upload current firmware
2. Test with Ruuvi Station app
3. Use nRF Connect to capture actual traffic
4. Compare with our protocol

### Phase 2: If Incompatible

1. Download `ruuvi.firmware.c`:
   ```bash
   git clone --recursive https://github.com/ruuvi/ruuvi.firmware.c.git
   cd ruuvi.firmware.c
   ```

2. Study these files:
   - `src/app_log.c` - Logging implementation
   - `src/app_comms.c` - Communication
   - `src/ruuvi_endpoints.h` - Endpoint system
   - `ble_services/ble_bulk_transfer.c` - Data transfer

3. Extract protocol specifics:
   - Exact message formats
   - Command codes
   - Response formats
   - Chunking strategy

4. Update our implementation to match

### Phase 3: ESP32 Port (If Needed)

If we need full compatibility:
1. Port essential Ruuvi modules to ESP32
2. Keep our hardware abstraction (board_config.h, sensors)
3. Replace only the NUS/history protocol layer
4. Test compatibility

## Key Insights from Official Firmware

### 1. Message Scheduling

Ruuvi processes BLE commands **asynchronously**:
```c
app_sched_event_put(&message, sizeof(message), 
                    ble_gatt_scheduler_event_handler);
```

This prevents blocking in BLE callbacks. Consider for our implementation:
```cpp
// In onWrite callback:
void onWrite(...) {
  // Parse command
  // Queue for processing in main loop
  command_queue.push(command);
}

// In loop():
while (!command_queue.empty()) {
  handleCommand(command_queue.pop());
}
```

### 2. Device Information Service (DIS)

They initialize DIS with:
- Manufacturer: "Ruuvi Innovations"
- Model number
- Serial number (from device ID)
- Hardware/Firmware/Software revisions

We should add this for better app compatibility.

### 3. DFU Service

They include DFU (Device Firmware Update) service. Not critical for history but good for completeness.

## Testing Strategy

### Immediate Testing

1. **Current firmware + Ruuvi Station**
   - Upload and test
   - Check serial logs for `[NUS] RX:` messages
   - Note any errors or unexpected behavior

2. **nRF Connect deep dive**
   - Connect and enable notifications
   - Send manual commands
   - Compare with protocol capture sample
   - Verify response format

### If Incompatible

1. **Clone official firmware**
   ```bash
   git clone --recursive https://github.com/ruuvi/ruuvi.firmware.c.git
   ```

2. **Study specific files** (can't build Nordic code on ESP32, but can read it)
   - Understand exact protocol
   - Extract command/response formats
   - Note any checksums or special handling

3. **Update our code to match**
   - Adjust message format
   - Add endpoint framing if needed
   - Update response format

## Conclusion

**Current Status:** 
- ✅ Our implementation is reasonable based on protocol capture
- ⚠️ May not match low-level NUS message format
- 🧪 Needs testing to verify compatibility

**Next Steps:**
1. **TEST** with Ruuvi Station (user has the app!)
2. **ANALYZE** traffic if it doesn't work
3. **CLONE** ruuvi.firmware.c for reference
4. **ADAPT** based on findings

The official firmware source is invaluable — it's the **ground truth**. However, since it's Nordic SDK specific, we can't directly compile it for ESP32. Instead, we:
- Study the protocol
- Extract the message formats
- Port the concepts (not the code) to ESP32/NimBLE

**Recommendation: Test first, then reference official code only if needed.**

## Resources

- Official firmware: https://github.com/ruuvi/ruuvi.firmware.c
- Protocol docs: https://docs.ruuvi.com/communication/bluetooth-connection/nordic-uart-service-nus
- RuuviTag forum: https://f.ruuvi.com
