# ESP32 RuuviTag Emulation for Victron Venus OS

Goal: broadcast a RuuviTag-compatible RAWv2 (Data Format 5) advertisement so Victron Venus OS can ingest temperature/humidity (and optionally pressure/accel) readings from an ESP32-C3/S3 using an external sensor (NTC or I2C).

## Advertisement spec (RAWv2 / DF5)

- Advertisement type: non-connectable, non-scannable, manufacturer data.
- Manufacturer ID: `0x0499` (Ruuvi).
- Payload layout (24 bytes total):
  - `0`: Data format = `0x05`.
  - `1-2`: Humidity, uint16, value * 0.0025 %RH.
  - `3-4`: Temperature, int16, value * 0.005 °C (two’s complement).
  - `5-6`: Pressure, uint16, value + 50000 Pa.
  - `7-8`: Acceleration X, int16, milli-g.
  - `9-10`: Acceleration Y, int16, milli-g.
  - `11-12`: Acceleration Z, int16, milli-g.
  - `13-14`: Power info, uint16:
    - bits 5-15: Battery mV = bits + 1600
    - bits 0-4: Tx power dBm = bits * 2 - 40
  - `15`: Movement counter, uint8 (increment on motion or each sample).
  - `16-17`: Measurement sequence number, uint16 (increment per report).
  - `18-23`: MAC address (big-endian as seen over the air).

Reference: Ruuvi firmware source and docs [https://github.com/ruuvi/ruuvitag_fw](https://github.com/ruuvi/ruuvitag_fw).

## ESP32 implementation outline

- BLE stack: NimBLE (ESP-IDF / Arduino NimBLE-Arduino) to keep footprint low.
- Advertise as legacy ADV, manufacturer data only, interval ~1000 ms (adjustable).
- Set public address (or static random) and mirror it in the payload MAC bytes.
- Sensor input:
  - I2C: SHT/HTU/BME family for temp/humidity; read at 1–5 s cadence.
  - NTC: ADC read, convert via Steinhart–Hart to °C; humidity optional/placeholder.
- Accel/power fields when unavailable:
  - Accel XYZ = 0.
  - Tx power: pick closest to actual (e.g., 0 or 4 dBm -> encode accordingly).
  - Battery: fixed value if powered by USB (e.g., 3300 mV -> encode).
  - Movement counter: increment each advertisement.

## Victron Venus OS expectations

- Venus OS already parses Ruuvi RAWv2; ensure:
  - Manufacturer ID = 0x0499.
  - Data format byte = 0x05.
  - MAC in payload matches advertiser address.
  - Temperature/humidity within sane ranges; avoid NaN encodings.

## Next steps

1) Stand up minimal ESP32 advertiser that emits static DF5 frame (no sensors) and verify Venus sees it.  
2) Wire one sensor (e.g., SHT31/SHTC3) and populate humidity/temperature fields.  
3) Add battery/tx power encoding helper and measurement counter.  
4) Optional: add motion source (e.g., INT pin from accelerometer) to drive movement counter.


