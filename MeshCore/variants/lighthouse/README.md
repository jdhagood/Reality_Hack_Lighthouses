# Lighthouse Variant Configuration

This variant configures MeshCore for the Lighthouse ESP32-S3 board with E22 LoRa module.

## Hardware Configuration

- **Board**: ESP32-S3 DevKitC-1
- **Radio**: E22 module (SX1262 for 900MHz or SX1268 for 400MHz)
- **Button**: GPIO 2
- **I2C**: SDA=GPIO 4, SCL=GPIO 5
- **LoRa SPI**: 
  - CS (NSS): GPIO 10
  - SCK: GPIO 12
  - MOSI: GPIO 11
  - MISO: GPIO 13
  - DIO1: GPIO 9
  - BUSY: GPIO 14
  - RESET: Not connected (-1)

## Available Build Environments

### SX1262 (900MHz) Variants
- `Lighthouse_sx1262_companion_radio_usb` - USB serial interface
- `Lighthouse_sx1262_companion_radio_ble` - BLE interface

### SX1268 (400MHz) Variants
- `Lighthouse_sx1268_companion_radio_usb` - USB serial interface
- `Lighthouse_sx1268_companion_radio_ble` - BLE interface

## Building and Flashing

### Build the firmware:
```bash
cd MeshCore
pio run -e Lighthouse_sx1262_companion_radio_usb
```

### Upload to device:
```bash
pio run -e Lighthouse_sx1262_companion_radio_usb -t upload
```

### Monitor serial output:
```bash
pio device monitor
```

Or combine upload and monitor:
```bash
pio run -e Lighthouse_sx1262_companion_radio_usb -t upload && pio device monitor
```

## Notes

- The default radio configuration uses SX1262 (900MHz). Use the `sx1268` environments for 400MHz modules.
- BLE variant includes BLE support with PIN code 123456 (configurable via build flags).
- USB variant communicates via USB serial port.
- The configuration matches the Meshtastic firmware variant in `firmware/variants/lighthouse/`.
