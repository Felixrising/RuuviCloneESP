#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include "history_log.h"

// Forward declarations
class NimBLEConnInfo;

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID        "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_CHARACTERISTIC_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // Write
#define NUS_TX_CHARACTERISTIC_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // Notify

// Command prefixes (from protocol analysis)
#define CMD_PREFIX_0 0x3A
#define CMD_PREFIX_1 0x3A
#define CMD_READ_HISTORY 0x11
#define CMD_TIME_SYNC 0x12  // Custom command for time synchronization

// Response prefixes
#define RESP_PREFIX 0x3A
#define RESP_STREAM_TEMP 0x30
#define RESP_STREAM_HUM  0x31
#define RESP_STREAM_PRES 0x32

// Maximum packet size for BLE
#define NUS_MAX_PACKET_SIZE 20

class HistoryNUS : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
  HistoryNUS() : server_(nullptr), service_(nullptr), 
                 rx_char_(nullptr), tx_char_(nullptr),
                 connected_(false), history_stream_active_(false) {}

  // Initialize NUS service and add to BLE server
  bool begin(NimBLEServer *server) {
    if (!server) {
      return false;
    }

    server_ = server;
    server_->setCallbacks(this);

    // Create Device Information Service (DIS) - Required by Ruuvi Station!
    NimBLEService *dis_service = server_->createService("180A");
    if (dis_service) {
      // Manufacturer Name
      NimBLECharacteristic *mfg_char = dis_service->createCharacteristic(
        "2A29", NIMBLE_PROPERTY::READ);
      mfg_char->setValue("Ruuvi Innovations");
      
      // Model Number
      NimBLECharacteristic *model_char = dis_service->createCharacteristic(
        "2A24", NIMBLE_PROPERTY::READ);
      model_char->setValue("RuuviTag");
      
      // Serial Number (use MAC address)
      NimBLECharacteristic *serial_char = dis_service->createCharacteristic(
        "2A25", NIMBLE_PROPERTY::READ);
      std::string mac = NimBLEDevice::getAddress().toString();
      serial_char->setValue(mac);
      
      // Firmware Revision
      NimBLECharacteristic *fw_char = dis_service->createCharacteristic(
        "2A26", NIMBLE_PROPERTY::READ);
      fw_char->setValue("v3.31.1a");
      
      // Hardware Revision
      NimBLECharacteristic *hw_char = dis_service->createCharacteristic(
        "2A27", NIMBLE_PROPERTY::READ);
      hw_char->setValue("ESP32-S3");
      
      dis_service->start();
      Serial.println("[DIS] Device Information Service initialized");
    }

    // Create Nordic UART Service
    service_ = server_->createService(NUS_SERVICE_UUID);
    if (!service_) {
      Serial.println("[NUS] Failed to create service");
      return false;
    }

    // Create RX characteristic (Write, WriteWithoutResponse)
    rx_char_ = service_->createCharacteristic(
      NUS_RX_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    rx_char_->setCallbacks(this);

    // Create TX characteristic (Notify)
    tx_char_ = service_->createCharacteristic(
      NUS_TX_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::NOTIFY
    );
    // Ensure CCCD is present so clients can enable notifications.
    NimBLEDescriptor *cccd = tx_char_->createDescriptor(
      "2902",
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    if (cccd) {
      const uint8_t cccd_value[2] = {0x00, 0x00};
      cccd->setValue(cccd_value, sizeof(cccd_value));
    }

    // Start service
    service_->start();
    
    Serial.println("[NUS] Service initialized");
    return true;
  }

  // Check if client is connected
  bool isConnected() const {
    return connected_;
  }

  // Send notification to client
  bool sendNotification(const uint8_t *data, size_t length) {
    if (!connected_ || !tx_char_) {
      return false;
    }

    if (length > NUS_MAX_PACKET_SIZE) {
      length = NUS_MAX_PACKET_SIZE;
    }

    tx_char_->setValue(data, length);
    tx_char_->notify();
    return true;
  }

  // NimBLEServerCallbacks
  void onConnect(NimBLEServer *server, NimBLEConnInfo& connInfo) {
    connected_ = true;
    
    // CRITICAL: Stop advertising when connected (single connection mode)
    NimBLEDevice::getAdvertising()->stop();
    
    Serial.printf("[NUS] Client connected from %s\n", 
                  connInfo.getAddress().toString().c_str());
    Serial.printf("[NUS] MTU: %u, Interval: %u, Latency: %u, Timeout: %u\n",
                  server->getPeerMTU(connInfo.getConnHandle()),
                  connInfo.getConnInterval(),
                  connInfo.getConnLatency(),
                  connInfo.getConnTimeout());
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo& connInfo, int reason) {
    connected_ = false;
    history_stream_active_ = false;
    Serial.printf("[NUS] Client disconnected (reason: %d = 0x%X)\n", reason, reason);
    
    // Common disconnect reasons:
    // 0x13 (19): Remote User Terminated Connection
    // 0x16 (22): Connection Terminated by Local Host
    // 0x08 (8): Connection Timeout
    // 0x3E (62): Connection Failed to be Established
    if (reason == 0x13) {
      Serial.println("[NUS] → Client initiated disconnection");
    } else if (reason == 0x16) {
      Serial.println("[NUS] → Host initiated disconnection");
    } else if (reason == 0x213) {
      Serial.println("[NUS] → Possible GATT/service discovery issue");
    }
    
    // CRITICAL: Restart advertising after disconnect so device is discoverable again
    NimBLEDevice::getAdvertising()->start();
    Serial.println("[NUS] Advertising restarted");
  }

  // NimBLECharacteristicCallbacks - handle received commands
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo& connInfo) {
    if (characteristic != rx_char_) {
      Serial.println("[NUS] Write to wrong characteristic!");
      return;
    }

    std::string value = characteristic->getValue();
    Serial.printf("[NUS] RX (%u bytes): ", value.length());
    
    const uint8_t *data = (const uint8_t *)value.data();
    size_t length = value.length();
    
    for (size_t i = 0; i < length; i++) {
      Serial.printf("%02X ", data[i]);
    }
    Serial.println();

    if (value.length() < 3) {
      Serial.println("[NUS] Message too short, ignoring");
      return;
    }

    // Parse command - check for Ruuvi Standard Message format
    if (length == 11) {
      // Ruuvi Standard Message: [dest][src][op][payload:8]
      uint8_t dest = data[0];
      uint8_t src = data[1];
      uint8_t op = data[2];
      Serial.printf("[NUS] Ruuvi message: dest=0x%02X src=0x%02X op=0x%02X\n", 
                    dest, src, op);
      
      // Handle based on operation
      if (op == 0x11) {  // LOG_VALUE_READ
        handleCommand(op, data + 3, length - 3);
      } else if (op == 0x12) {  // Custom time sync
        handleCommand(op, data + 3, length - 3);
      } else {
        Serial.printf("[NUS] Unsupported operation: 0x%02X\n", op);
      }
    } else {
      Serial.printf("[NUS] Non-standard message length: %u\n", length);
    }
  }

private:
  NimBLEServer *server_;
  NimBLEService *service_;
  NimBLECharacteristic *rx_char_;
  NimBLECharacteristic *tx_char_;
  bool connected_;
  bool history_stream_active_;

  void handleCommand(uint8_t cmd, const uint8_t *params, size_t param_length) {
    switch (cmd) {
      case CMD_READ_HISTORY:
        handleReadHistory(params, param_length);
        break;
      
      case CMD_TIME_SYNC:
        handleTimeSync(params, param_length);
        break;
      
      default:
        Serial.printf("[NUS] Unknown command: 0x%02X\n", cmd);
        break;
    }
  }

  void handleTimeSync(const uint8_t *params, size_t param_length) {
    if (param_length < 4) {
      Serial.println("[NUS] Time sync: invalid params");
      return;
    }

    // Parse Unix timestamp (big-endian)
    uint32_t timestamp = ((uint32_t)params[0] << 24) |
                        ((uint32_t)params[1] << 16) |
                        ((uint32_t)params[2] << 8) |
                        ((uint32_t)params[3]);

    Serial.printf("[NUS] Time sync request: %u (0x%08X)\n", timestamp, timestamp);

    // Calculate offset: timestamp - (millis/1000)
    uint32_t current_millis_sec = millis() / 1000;
    uint32_t offset = timestamp - current_millis_sec;
    
    g_history_log.setRTCOffset(offset);
    
    Serial.printf("[NUS] RTC offset set: %u, current time: %u\n", 
                  offset, g_history_log.getCurrentTimestamp());
    
    // Send acknowledgment
    uint8_t ack[5];
    ack[0] = RESP_PREFIX;
    ack[1] = 0x12;  // Time sync response
    ack[2] = (timestamp >> 24) & 0xFF;
    ack[3] = (timestamp >> 16) & 0xFF;
    ack[4] = (timestamp >> 8) & 0xFF;
    
    sendNotification(ack, sizeof(ack));
  }

  void handleReadHistory(const uint8_t *params, size_t param_length) {
    Serial.println("[NUS] History read request");

    // Parse parameters
    // Expected format: timestamp(4) + params(4) = 8 bytes
    uint32_t start_timestamp = 0;
    if (param_length >= 4) {
      // Big-endian timestamp
      start_timestamp = ((uint32_t)params[0] << 24) |
                        ((uint32_t)params[1] << 16) |
                        ((uint32_t)params[2] << 8) |
                        ((uint32_t)params[3]);
    }

    Serial.printf("[NUS] Start timestamp: %u (0x%08X)\n", start_timestamp, start_timestamp);

    // Read history entries
    std::vector<HistoryEntry> entries;
    if (!g_history_log.readAllEntries(entries)) {
      Serial.println("[NUS] Failed to read history");
      return;
    }

    Serial.printf("[NUS] Streaming %u entries\n", entries.size());

    if (entries.size() == 0) {
      Serial.println("[NUS] No history data available yet");
      // Send empty response to acknowledge command
      // This prevents client timeout/disconnect
      uint8_t ack[11] = {0x3A, 0x3A, 0x10, 0, 0, 0, 0, 0, 0, 0, 0};
      sendNotification(ack, sizeof(ack));
      return;
    }

    // Stream entries to client
    // Each sensor type is sent as a separate stream (0x30=temp, 0x31=hum, 0x32=pres)
    for (const auto &entry : entries) {
      // Filter by timestamp if specified
      if (start_timestamp > 0 && entry.timestamp < start_timestamp) {
        continue;
      }

      // Convert DF5 encoded values to Ruuvi Endpoints format
      // DF5 uses: temp*200 (0.005°C), hum*400 (0.0025%), pres (Pa)
      // RE uses: temp*100 (0.01°C), hum*100 (0.01%), pres (Pa)
      
      // Temperature: DF5 int16 (0.005°C) → RE int32 (0.01°C)
      // Divide by 2 to convert from 0.005°C steps to 0.01°C steps
      int32_t temp_re = (int32_t)entry.temperature / 2;
      
      // Humidity: DF5 uint16 (0.0025%) → RE int32 (0.01%)
      // Divide by 4 to convert from 0.0025% steps to 0.01% steps
      int32_t hum_re = (int32_t)entry.humidity / 4;
      
      // Pressure: DF5 uint16 (1 Pa offset 50000) → RE int32 (1 Pa, no offset)
      // Add back the 50000 Pa offset that DF5 removes
      int32_t pres_re = (int32_t)entry.pressure + 50000;
      
      // Send temperature
      sendHistoryPacket(RESP_STREAM_TEMP, entry.timestamp, temp_re);
      delay(5);  // Small delay to avoid overwhelming BLE stack

      // Send humidity
      sendHistoryPacket(RESP_STREAM_HUM, entry.timestamp, hum_re);
      delay(5);

      // Send pressure
      sendHistoryPacket(RESP_STREAM_PRES, entry.timestamp, pres_re);
      delay(5);
    }

    Serial.printf("[NUS] History stream complete (%u entries sent)\n", entries.size());
  }

  void sendHistoryPacket(uint8_t stream_id, uint32_t timestamp, int32_t value) {
    // Packet format (11 bytes) - Ruuvi Standard Message:
    // [0]: Destination endpoint (0x3A = Environmental)
    // [1]: Source endpoint (stream_id: 0x30=temp, 0x31=hum, 0x32=pres)
    // [2]: Operation (0x10 = RE_STANDARD_LOG_VALUE_WRITE)
    // [3-6]: Timestamp (big-endian uint32, seconds since epoch)
    // [7-10]: Value (big-endian int32, scaled)
    
    uint8_t packet[11];
    packet[0] = RESP_PREFIX;  // 0x3A = Environmental endpoint
    packet[1] = stream_id;    // Source: 0x30/0x31/0x32
    packet[2] = 0x10;         // RE_STANDARD_LOG_VALUE_WRITE
    
    // Timestamp (big-endian uint32)
    packet[3] = (timestamp >> 24) & 0xFF;
    packet[4] = (timestamp >> 16) & 0xFF;
    packet[5] = (timestamp >> 8) & 0xFF;
    packet[6] = timestamp & 0xFF;
    
    // Value (big-endian int32) - FIXED: was uint16, now int32!
    packet[7] = (value >> 24) & 0xFF;
    packet[8] = (value >> 16) & 0xFF;
    packet[9] = (value >> 8) & 0xFF;
    packet[10] = value & 0xFF;

    sendNotification(packet, sizeof(packet));
  }
};

// Global NUS instance
extern HistoryNUS g_history_nus;
