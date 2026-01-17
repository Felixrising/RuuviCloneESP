#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

// History logging configuration
#ifndef HISTORY_LOG_ENABLE
#define HISTORY_LOG_ENABLE 1  // Enable by default
#endif

#ifndef HISTORY_INTERVAL_SEC
#define HISTORY_INTERVAL_SEC 300  // 5 minutes (300 seconds)
#endif

#ifndef HISTORY_MAX_DAYS
#define HISTORY_MAX_DAYS 10  // 10 days of history
#endif

// Filesystem configuration
#ifndef HISTORY_FS_PARTITION_LABEL
#define HISTORY_FS_PARTITION_LABEL ""  // Empty = use default partition
#endif

#ifndef HISTORY_FS_SIZE_KB
#define HISTORY_FS_SIZE_KB 128  // 128 KB for history storage
#endif

// File paths
#define HISTORY_DATA_FILE "/history.bin"
#define HISTORY_INDEX_FILE "/history.idx"
#define HISTORY_CONFIG_FILE "/history.cfg"

// History entry structure (aligned for efficient storage)
#pragma pack(push, 1)
struct HistoryEntry {
  uint32_t timestamp;      // Unix timestamp (seconds since epoch)
  int16_t temperature;     // Temperature in 0.005°C units (Ruuvi DF5 format)
  uint16_t humidity;       // Humidity in 0.0025% units (Ruuvi DF5 format)
  uint16_t pressure;       // Pressure in 1 Pa units (Ruuvi DF5 format)
  int16_t accel_x;         // Acceleration X in mg
  int16_t accel_y;         // Acceleration Y in mg
  int16_t accel_z;         // Acceleration Z in mg
  uint16_t battery_mv;     // Battery voltage in mV
  uint8_t movement_count;  // Movement counter
  uint8_t reserved;        // Padding for alignment
  // Total: 22 bytes per entry
};
#pragma pack(pop)

// History metadata (stored in index file)
#pragma pack(push, 1)
struct HistoryMetadata {
  uint32_t magic;           // Magic bytes to verify integrity (0x52555649 = "RUVI")
  uint32_t version;         // Format version (1)
  uint32_t entry_count;     // Total entries written (wraps at max)
  uint32_t oldest_index;    // Index of oldest valid entry
  uint32_t newest_index;    // Index of newest entry
  uint32_t max_entries;     // Maximum entries (calculated from storage)
  uint32_t rtc_offset;      // RTC offset for time sync (seconds)
  uint32_t last_write_time; // Last write timestamp
  uint8_t checksum;         // Simple XOR checksum
  uint8_t reserved[7];      // Reserved for future use
  // Total: 40 bytes
};
#pragma pack(pop)

#define HISTORY_MAGIC 0x52555649  // "RUVI" in hex

class HistoryLog {
public:
  HistoryLog() : initialized_(false), rtc_offset_(0), last_write_time_(0) {}

  // Initialize history logging system
  bool begin() {
    if (!HISTORY_LOG_ENABLE) {
      Serial.println("[HISTORY] Logging disabled");
      return false;
    }

    // Mount LittleFS
    if (!LittleFS.begin(true)) {  // true = format if mount fails
      Serial.println("[HISTORY] Failed to mount LittleFS");
      return false;
    }

    // Load or create metadata
    if (!loadMetadata()) {
      if (!createNewMetadata()) {
        Serial.println("[HISTORY] Failed to create metadata");
        return false;
      }
    }

    initialized_ = true;
    Serial.printf("[HISTORY] Initialized: %u entries, max %u\n", 
                  meta_.entry_count, meta_.max_entries);
    return true;
  }

  // Log a new entry
  bool logEntry(const HistoryEntry &entry) {
    if (!initialized_) {
      return false;
    }

    // Calculate write position (circular buffer)
    uint32_t write_index = meta_.newest_index;
    if (meta_.entry_count > 0) {
      write_index = (write_index + 1) % meta_.max_entries;
    }

    // Open data file for writing
    File dataFile = LittleFS.open(HISTORY_DATA_FILE, "r+");
    if (!dataFile) {
      Serial.println("[HISTORY] Failed to open data file");
      return false;
    }

    // Seek to write position
    size_t offset = write_index * sizeof(HistoryEntry);
    if (!dataFile.seek(offset, SeekSet)) {
      Serial.println("[HISTORY] Failed to seek");
      dataFile.close();
      return false;
    }

    // Write entry
    size_t written = dataFile.write((const uint8_t *)&entry, sizeof(HistoryEntry));
    dataFile.close();

    if (written != sizeof(HistoryEntry)) {
      Serial.println("[HISTORY] Write failed");
      return false;
    }

    // Update metadata
    meta_.newest_index = write_index;
    meta_.entry_count++;
    meta_.last_write_time = entry.timestamp;

    // If buffer is full, move oldest pointer
    if (meta_.entry_count > meta_.max_entries) {
      meta_.oldest_index = (meta_.oldest_index + 1) % meta_.max_entries;
      meta_.entry_count = meta_.max_entries;
    }

    // Save metadata
    saveMetadata();

    Serial.printf("[HISTORY] Logged entry %u at index %u (ts=%u)\n", 
                  meta_.entry_count, write_index, entry.timestamp);
    return true;
  }

  // Read entries in time range
  bool readEntries(uint32_t start_timestamp, uint32_t end_timestamp, 
                   std::vector<HistoryEntry> &entries, uint32_t max_count = 0) {
    if (!initialized_ || meta_.entry_count == 0) {
      return false;
    }

    entries.clear();
    File dataFile = LittleFS.open(HISTORY_DATA_FILE, "r");
    if (!dataFile) {
      return false;
    }

    // Read entries from oldest to newest
    uint32_t count = (meta_.entry_count < meta_.max_entries) ? meta_.entry_count : meta_.max_entries;
    uint32_t read_count = 0;

    for (uint32_t i = 0; i < count; i++) {
      uint32_t index = (meta_.oldest_index + i) % meta_.max_entries;
      size_t offset = index * sizeof(HistoryEntry);

      if (!dataFile.seek(offset, SeekSet)) {
        continue;
      }

      HistoryEntry entry;
      size_t read_size = dataFile.read((uint8_t *)&entry, sizeof(HistoryEntry));
      if (read_size != sizeof(HistoryEntry)) {
        continue;
      }

      // Filter by timestamp
      if (entry.timestamp >= start_timestamp && entry.timestamp <= end_timestamp) {
        entries.push_back(entry);
        read_count++;

        if (max_count > 0 && read_count >= max_count) {
          break;
        }
      }
    }

    dataFile.close();
    Serial.printf("[HISTORY] Read %u entries (ts %u-%u)\n", 
                  entries.size(), start_timestamp, end_timestamp);
    return true;
  }

  // Get all entries (for full download)
  bool readAllEntries(std::vector<HistoryEntry> &entries) {
    return readEntries(0, 0xFFFFFFFF, entries);
  }

  // Get metadata
  const HistoryMetadata &getMetadata() const {
    return meta_;
  }

  // Set RTC offset for time synchronization
  void setRTCOffset(uint32_t offset) {
    rtc_offset_ = offset;
    meta_.rtc_offset = offset;
    saveMetadata();
    Serial.printf("[HISTORY] RTC offset set to %u\n", offset);
  }

  // Get current timestamp (with RTC offset)
  uint32_t getCurrentTimestamp() const {
    return (millis() / 1000) + rtc_offset_;
  }

  // Clear all history
  bool clear() {
    if (!initialized_) {
      return false;
    }

    // Delete files
    LittleFS.remove(HISTORY_DATA_FILE);
    LittleFS.remove(HISTORY_INDEX_FILE);

    // Recreate metadata
    return createNewMetadata();
  }

  // Get storage statistics
  void getStats(uint32_t &total_entries, uint32_t &max_entries, 
                uint32_t &oldest_timestamp, uint32_t &newest_timestamp) {
    total_entries = meta_.entry_count;
    max_entries = meta_.max_entries;
    oldest_timestamp = 0;
    newest_timestamp = meta_.last_write_time;

    // Read oldest entry timestamp
    if (initialized_ && meta_.entry_count > 0) {
      File dataFile = LittleFS.open(HISTORY_DATA_FILE, "r");
      if (dataFile) {
        size_t offset = meta_.oldest_index * sizeof(HistoryEntry);
        if (dataFile.seek(offset, SeekSet)) {
          HistoryEntry entry;
          if (dataFile.read((uint8_t *)&entry, sizeof(HistoryEntry)) == sizeof(HistoryEntry)) {
            oldest_timestamp = entry.timestamp;
          }
        }
        dataFile.close();
      }
    }
  }

private:
  bool initialized_;
  HistoryMetadata meta_;
  uint32_t rtc_offset_;
  uint32_t last_write_time_;

  bool loadMetadata() {
    File indexFile = LittleFS.open(HISTORY_INDEX_FILE, "r");
    if (!indexFile) {
      return false;
    }

    size_t read_size = indexFile.read((uint8_t *)&meta_, sizeof(HistoryMetadata));
    indexFile.close();

    if (read_size != sizeof(HistoryMetadata)) {
      return false;
    }

    // Verify magic and checksum
    if (meta_.magic != HISTORY_MAGIC || !verifyChecksum()) {
      Serial.println("[HISTORY] Invalid metadata");
      return false;
    }

    rtc_offset_ = meta_.rtc_offset;
    return true;
  }

  bool saveMetadata() {
    meta_.magic = HISTORY_MAGIC;
    meta_.version = 1;
    calculateChecksum();

    File indexFile = LittleFS.open(HISTORY_INDEX_FILE, "w");
    if (!indexFile) {
      return false;
    }

    size_t written = indexFile.write((const uint8_t *)&meta_, sizeof(HistoryMetadata));
    indexFile.close();

    return (written == sizeof(HistoryMetadata));
  }

  bool createNewMetadata() {
    // Calculate max entries based on available storage
    // Reserve some space for filesystem overhead
    size_t available_bytes = HISTORY_FS_SIZE_KB * 1024 - 4096;  // Reserve 4KB
    uint32_t max_entries = available_bytes / sizeof(HistoryEntry);

    // Cap at 10 days worth of 5-minute samples
    uint32_t max_days_entries = (HISTORY_MAX_DAYS * 24 * 60) / (HISTORY_INTERVAL_SEC / 60);
    if (max_entries > max_days_entries) {
      max_entries = max_days_entries;
    }

    memset(&meta_, 0, sizeof(HistoryMetadata));
    meta_.magic = HISTORY_MAGIC;
    meta_.version = 1;
    meta_.entry_count = 0;
    meta_.oldest_index = 0;
    meta_.newest_index = 0;
    meta_.max_entries = max_entries;
    meta_.rtc_offset = 0;
    meta_.last_write_time = 0;

    // Create empty data file
    File dataFile = LittleFS.open(HISTORY_DATA_FILE, "w");
    if (!dataFile) {
      return false;
    }

    // Pre-allocate space (write zeros)
    size_t total_size = max_entries * sizeof(HistoryEntry);
    uint8_t zero_buffer[256];
    memset(zero_buffer, 0, sizeof(zero_buffer));

    for (size_t written = 0; written < total_size; written += sizeof(zero_buffer)) {
      size_t chunk = (total_size - written < sizeof(zero_buffer)) ? 
                     (total_size - written) : sizeof(zero_buffer);
      dataFile.write(zero_buffer, chunk);
    }
    dataFile.close();

    Serial.printf("[HISTORY] Created new metadata: %u entries, %u bytes\n", 
                  max_entries, total_size);

    return saveMetadata();
  }

  void calculateChecksum() {
    uint8_t checksum = 0;
    const uint8_t *data = (const uint8_t *)&meta_;
    for (size_t i = 0; i < sizeof(HistoryMetadata) - sizeof(meta_.checksum) - sizeof(meta_.reserved); i++) {
      checksum ^= data[i];
    }
    meta_.checksum = checksum;
  }

  bool verifyChecksum() {
    uint8_t stored_checksum = meta_.checksum;
    calculateChecksum();
    bool valid = (stored_checksum == meta_.checksum);
    meta_.checksum = stored_checksum;  // Restore original
    return valid;
  }
};

// Global history log instance
extern HistoryLog g_history_log;
