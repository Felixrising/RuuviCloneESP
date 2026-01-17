# Reference Materials

Collected resources for implementing a Ruuvi-like BLE temperature/humidity beacon on ESP32 (C3/S3) with external NTC or I2C sensors.

## Provided links

- Ruuvi forum thread on ESP32 communication protocol notes: https://f.ruuvi.com/t/conneect-ruuvitag-with-esp32/6720/3
- Hackster project “BLE Weather Station with ESP32 and Ruuvi”: https://www.hackster.io/amir-pournasserian/ble-weather-station-with-esp32-and-ruuvi-e8a68d
- ESP32 Ruuvitag data collector (MQTT/Spiffs): https://github.com/hpirila/ESP32-Ruuvitag-Collector
- ESP32 Weather Station example: https://github.com/mkjanke/ESP32-Weather-Station
- Python RuuviTag sensor library: https://github.com/ttu/ruuvitag-sensor
- RuuviCollector backend: https://github.com/Scrin/RuuviCollector
- Official RuuviTag firmware source: https://github.com/ruuvi/ruuvitag_fw

## Notes

- Use these for protocol details (adv payload formats, history), BLE scanning/advertising patterns, and data formatting ideas.
- Keep external sensor choice flexible (NTC via ADC or I2C sensor like SHT/HTU/BME family); focus on producing compatible advertisement frames.


