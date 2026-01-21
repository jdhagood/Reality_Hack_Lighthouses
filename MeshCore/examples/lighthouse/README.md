# Lighthouse Network Firmware

Firmware for 30 ESP32-S3 lighthouse boards that communicate on a private mesh network channel.

## Features

- Each lighthouse has a unique number (1-30) set at build time
- Button press on GPIO 2 sends: "Lighthouse <NUMBER>: Button Pressed" on private channel
- Radio configured for US region: 910.525 MHz, 62.5 kHz BW, SF 7, CR 5, 22 dBm
- BLE pairing support for configuration and monitoring
- All lighthouses share the same private channel PSK

## Building

### For Lighthouse Number N (1-30):

**For 900MHz SX1262 variant:**
```bash
cd MeshCore
pio run -e Lighthouse_sx1262_N -t upload
```

Example for lighthouse #1:
```bash
pio run -e Lighthouse_sx1262_1 -t upload
```

**For 400MHz SX1268 variant:**
```bash
cd MeshCore
pio run -e Lighthouse_sx1268_N -t upload
```

Example for lighthouse #1:
```bash
pio run -e Lighthouse_sx1268_1 -t upload
```

### Building for All 30 Lighthouses

You can create a simple script to build all 30:

```bash
#!/bin/bash
for i in {1..30}; do
  echo "Building lighthouse #$i..."
  pio run -e Lighthouse_sx1262_lighthouse -D LIGHTHOUSE_NUMBER=$i -t upload
  read -p "Press enter to continue to next lighthouse..."
done
```

## Radio Configuration

- **Frequency**: 910.525 MHz (US ISM band)
- **Bandwidth**: 62.5 kHz
- **Spreading Factor**: 7
- **Coding Rate**: 5
- **Transmit Power**: 22 dBm

## Private Channel

All lighthouses use the same private channel:
- **Channel Name**: "Lighthouse Network"
- **PSK**: Shared across all 30 lighthouses (defined in code)

## Button Operation

- **Pin**: GPIO 2 (with internal pull-up)
- **Action**: Press button to send message on private channel
- **Message Format**: "Lighthouse <NUMBER>: Button Pressed"
- **Rate Limiting**: 2 second cooldown between sends

## BLE Pairing

- **BLE Name**: "Lighthouse-<NUMBER>"
- **PIN**: 100000 + lighthouse number (e.g., lighthouse #5 = PIN 100005)
- Connect using MeshCore mobile app or compatible client

## Serial Monitor

Connect via USB serial at 115200 baud to see:
- Startup messages
- Button press notifications
- Received channel messages
- Debug information

## Testing

1. Build and flash lighthouse #1
2. Build and flash lighthouse #2
3. Press button on lighthouse #1
4. Check serial monitor on lighthouse #2 - should see the message
5. Repeat for all 30 lighthouses

## Troubleshooting

- **No messages received**: Check that all lighthouses are on the same channel and using same radio parameters
- **Button not working**: Verify GPIO 2 is connected and not shorted
- **BLE not connecting**: Check PIN code (100000 + lighthouse number)
- **Build errors**: Ensure LIGHTHOUSE_NUMBER is defined (1-30)


## Audio

```
ffmpeg -i button.mp3 -ac 1 -ar 11025 -c:a pcm_u8 button.wav
```