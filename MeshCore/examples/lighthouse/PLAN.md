# Lighthouse Firmware Implementation Plan

## Overview
Create a custom lighthouse firmware for 30 ESP32-S3 boards that:
- Each has a unique number (1-30) set at build time
- Sends button press messages on a private channel
- Uses specific radio parameters for US region
- Maintains BLE pairing capability

## Requirements

### Hardware Configuration
- Board: ESP32-S3 DevKitC-1 (Lighthouse variant)
- Button: GPIO 2
- Radio: E22 module (SX1262/SX1268)
- BLE: Enabled for pairing

### Radio Parameters (US Region)
- Frequency: 910.525 MHz
- Bandwidth: 62.5 kHz
- Spreading Factor: 7
- Coding Rate: 5
- Transmit Power: 22 dBm

### Functionality
- Button press on GPIO 2 sends: "Lighthouse <NUMBER>: Button Pressed"
- Message sent on preconfigured private channel
- BLE pairing like companion_radio example
- Each lighthouse has unique number 1-30

## Implementation Steps

### 1. Create Lighthouse Example Directory
- Create `MeshCore/examples/lighthouse/` directory
- Based on companion_radio example structure
- Simplified for lighthouse-specific use case

### 2. Create Main Application Files
- `main.cpp`: Main application loop with button handling
- `LighthouseMesh.h/cpp`: Custom mesh class extending BaseChatMesh
- `LighthouseMesh.h`: Header with lighthouse-specific logic

### 3. Build Configuration
- Add build flag: `-D LIGHTHOUSE_NUMBER=<N>` (1-30)
- Create build environments in `MeshCore/variants/lighthouse/platformio.ini`
- Support both SX1262 and SX1268 variants
- Include BLE support

### 4. Radio Configuration
- Set LORA_FREQ=910.525
- Set LORA_BW=62.5 (kHz)
- Set LORA_SF=7
- Set LORA_CR=5
- Set LORA_TX_POWER=22

### 5. Private Channel Setup
- Create/configure private channel with shared PSK
- Channel name: "Lighthouse Network" or similar
- All 30 lighthouses use same channel PSK
- Initialize channel in setup()

### 6. Button Handling
- Monitor GPIO 2 button state
- Detect button press (debounce)
- On press: send message via sendGroupMessage()
- Message format: "Lighthouse <LIGHTHOUSE_NUMBER>: Button Pressed"

### 7. BLE Interface
- Include SerialBLEInterface like companion_radio
- BLE PIN code configuration
- Maintain compatibility with MeshCore clients

## Files to Create

1. `MeshCore/examples/lighthouse/main.cpp`
   - Setup and loop functions
   - Button monitoring
   - BLE interface initialization

2. `MeshCore/examples/lighthouse/LighthouseMesh.h`
   - Class definition extending BaseChatMesh
   - Lighthouse-specific methods

3. `MeshCore/examples/lighthouse/LighthouseMesh.cpp`
   - Implementation of lighthouse mesh logic
   - Channel message handling

4. Update `MeshCore/variants/lighthouse/platformio.ini`
   - Add lighthouse example environments
   - Support lighthouse number parameter
   - Radio parameter configuration

## Build Commands

For lighthouse number N (1-30):
```bash
pio run -e Lighthouse_lighthouse_N -t upload
```

Or use build flag directly:
```bash
pio run -e Lighthouse_sx1262_lighthouse -D LIGHTHOUSE_NUMBER=5 -t upload
```

## Private Channel PSK

All lighthouses need the same channel PSK. Options:
1. Hardcode a shared PSK in the firmware
2. Generate PSK from a shared secret
3. Use a predefined PSK (base64 encoded)

Recommended: Use a predefined PSK like companion_radio's PUBLIC_GROUP_PSK pattern.

## Testing Strategy

1. Build firmware for lighthouse #1
2. Flash and test button press
3. Verify message appears on channel
4. Build and flash lighthouse #2
5. Test communication between lighthouses
6. Scale to all 30 units

## Notes

- Bandwidth clarification: User specified 62.5 Hz, but LoRa typically uses kHz. Will implement as 62.5 kHz (common LoRa bandwidth). If 62.5 Hz is truly needed, this would require custom radio configuration.
- Button debouncing: Implement proper debouncing to avoid multiple sends
- Message rate limiting: Consider rate limiting to prevent spam
- Channel persistence: Channel should persist across reboots
