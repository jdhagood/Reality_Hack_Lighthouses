# Lighthouse Firmware (Minimal Build)

This is a minimal, lighthouse-only build tree for PlatformIO.

## Prerequisites

- PlatformIO CLI installed (`pio`)
- USB drivers for your ESP32-S3 board

## Build

```bash
pio run -e lighthouse
```

Build outputs land under `.pio/build/lighthouse/`.

## Flash

Plug in the board, then:

```bash
pio run -e lighthouse -t upload
```

If you have multiple serial ports, set the upload port:

```bash
pio run -e lighthouse -t upload --upload-port COMx
```

## Monitor

```bash
pio device monitor -e lighthouse
```

If you need to specify a port:

```bash
pio device monitor -e lighthouse --port COMx
```
