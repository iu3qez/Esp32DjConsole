# ESP32 DJ Console - Hercules DJ Console MP3 e2 to Thetis CAT/TCI Bridge

## Project Overview

ESP32-S3 firmware that acts as a USB host for the Hercules DJ Console MP3 e2 (VID: 0x06f8, PID: 0xb105), translates its inputs to CAT/TCI commands over TCP/WebSocket, and serves a Svelte web GUI for mapping customization.

## Architecture

```
[Hercules DJ Console MP3 e2]
        |  USB (bulk IN, 38-byte state packets)
        v
   [ESP32-S3]  -- USB Host (GPIO19=D-, GPIO20=D+)
        |
        +-- WiFi (STA mode, connects to local network)
        |
        +-- TCI WebSocket client --> Thetis (ws://host:50001)
        |   (frequency, mode, volume, filters, PTT, etc.)
        |
        +-- HTTP server (port 80)
            +-- Svelte SPA (LittleFS partition)
            +-- REST API for configuration
            +-- WebSocket for live status
```

## ESP-IDF Setup

### Prerequisites

```bash
# Install ESP-IDF v5.5.x (recommended)
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

### Activate ESP-IDF Environment

```bash
# Run this in every new terminal session before building
source ~/esp/esp-idf/export.sh

# Or add an alias to your shell profile:
# alias get_idf='source ~/esp/esp-idf/export.sh'
```

### Build and Flash

```bash
# Set target to ESP32-S3
idf.py set-target esp32s3

# Configure (menuconfig)
idf.py menuconfig
# Enable: Component config -> HTTP Server -> WebSocket server support
# Enable: Component config -> USB Host Library

# Build
idf.py build

# Flash and monitor (replace /dev/ttyUSB0 with your port)
idf.py -p /dev/ttyUSB0 flash monitor

# Monitor only (Ctrl+] to exit)
idf.py -p /dev/ttyUSB0 monitor
```

### Frontend Build (Svelte)

```bash
cd frontend
npm install
npm run build

# Then rebuild firmware to embed updated frontend
cd ..
idf.py build
```

## Project Structure

```
Esp32DjConsole/
  CMakeLists.txt              # Top-level ESP-IDF project
  partitions.csv              # Custom partition table (app + www + nvs)
  sdkconfig.defaults          # Default ESP-IDF config overrides
  main/
    CMakeLists.txt            # Main component build config
    main.c                    # Entry point: WiFi, USB host, HTTP server init
    usb_dj_host.c             # USB host driver for Hercules DJ Console
    usb_dj_host.h
    tci_client.c              # TCI WebSocket client for Thetis
    tci_client.h
    cat_client.c              # Kenwood CAT TCP client (fallback)
    cat_client.h
    mapping_engine.c          # DJ control -> CAT/TCI command mapping
    mapping_engine.h
    http_server.c             # HTTP server: static files + REST API + WS
    http_server.h
    config_store.c            # NVS-based persistent configuration
    config_store.h
  frontend/                   # Svelte SPA
    src/
    package.json
    vite.config.js
    svelte.config.js
  sample.ino                  # Reference: original Teensy USB host driver
```

## Key Technical Details

### USB Protocol (Hercules DJ Console MP3 e2)

- **VID/PID:** 0x06f8 / 0xb105
- **Interface:** 0 (vendor-specific, not HID)
- **Bulk IN endpoint:** 0x81 (EP1 IN), 38-byte state packets, 64-byte MPS
- **Init sequence:** 14 vendor-specific control transfers (see sample.ino)
- **State format:** 38 bytes, bitmask-based for buttons, 0x00-0xFF for dials/sliders

### Controls (from sample.ino Mappings[])

- **Buttons (type 0):** 46 buttons (Play, Cue, Sync, Load, FWD, RWD, N1-N8 per deck, etc.)
- **Dials/Sliders (type 1):** Treble/Medium/Bass/Vol per deck, XFader (0x00-0xFF)
- **Jog wheels (type 2):** Jog_A, Jog_B, Pitch_A, Pitch_B (rotary encoders, 0x00-0xFF wrap)

### TCI Protocol (Thetis)

- **Transport:** WebSocket, default port 50001
- **Format:** ASCII text, `command:arg1,arg2,...;`
- **Key commands:** `vfo:trx,vfo,freq;`, `modulation:trx,mode;`, `volume:level;`, `drive:level;`, `trx:trx,state;`, `rx_smeter:trx,vfo,level;`

### Kenwood CAT (fallback)

- **Transport:** TCP
- **Format:** ASCII, `COMMAND[params];`
- **Key commands:** `ZZFA` (freq), `ZZMD` (mode), `ZZAG` (volume), `ZZTX` (PTT), `ZZSM` (S-meter)

## Hardware Wiring

```
ESP32-S3 GPIO19 --> USB D- (white)
ESP32-S3 GPIO20 --> USB D+ (green)
External 5V     --> USB VBUS (red)   [ESP32 cannot source 5V!]
GND             --> USB GND (black)
```

## Coding Conventions

- Pure C (ESP-IDF style), no C++ unless necessary
- FreeRTOS tasks for USB host, TCI client, and HTTP server
- Use ESP_LOG macros for logging (ESP_LOGI, ESP_LOGW, ESP_LOGE)
- Config stored in NVS (Non-Volatile Storage)
- Control mappings stored as JSON in NVS, editable via web GUI
