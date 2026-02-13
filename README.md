# ESP32 DJ Console

ESP32-S3 firmware that bridges a **Hercules DJ Console MP3 e2** to a **Thetis SDR** via TCI (WebSocket) or CAT (TCP) commands. A built-in web GUI allows live monitoring and configuration.

## What it does

```
[Hercules DJ Console MP3 e2]
        |  USB bulk (38-byte state packets)
        v
   [ESP32-S3]
        |
        +-- TCI WebSocket --> Thetis SDR (ws://host:50001)
        |   Knobs/buttons map to VFO, mode, volume, PTT, etc.
        |
        +-- CAT TCP ------> Thetis SDR (tcp://host:31001)
        |   Fallback protocol (Kenwood ZZ extended commands)
        |
        +-- HTTP :80
            +-- Svelte SPA (dashboard, mapping editor, debug console)
            +-- REST API for configuration
            +-- WebSocket for live control/radio state updates
```

Every knob, slider, button, and jog wheel on the DJ console maps to a radio command. Mappings are fully configurable via the web interface and stored in flash (NVS).

## Hardware

- **ESP32-S3** dev board (4MB flash minimum)
- **Hercules DJ Console MP3 e2** (VID: 0x06f8, PID: 0xb105)
- External 5V supply for USB VBUS (ESP32 cannot source 5V)

### Wiring

| ESP32-S3 | USB Cable |
|----------|-----------|
| GPIO 19  | D- (white) |
| GPIO 20  | D+ (green) |
| GND      | GND (black) |
| External 5V | VBUS (red) |

## Building

### Prerequisites

- [ESP-IDF v5.5.x](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/)
- Node.js 18+ and npm (for frontend build)

### Build firmware

```bash
# Activate ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Build frontend first
cd frontend
npm install
npm run build
cd ..

# Build and flash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Upload web files to SPIFFS

After building the frontend, flash the www partition:

```bash
# Create SPIFFS image from build output and flash it
python $IDF_PATH/components/spiffs/spiffsgen.py 0x100000 build/www build/www.bin
esptool.py --chip esp32s3 write_flash 0x1F0000 build/www.bin
```

### Development (frontend only)

```bash
cd frontend
npm run dev
# Opens at localhost:5173, proxies API calls to djconsole.local
```

## Web Interface

After flashing, access at `http://djconsole.local` (or the device IP).

- **Dashboard** - Connection status (USB, TCI, CAT), radio state (VFO, mode, TX), heap usage
- **Mappings** - View and edit all 59 control mappings, save to flash
- **Config** - WiFi credentials, TCI/CAT host and port, protocol selection, debug level
- **Debug** - Live feed of DJ console control events (useful for verifying mappings)

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | System status, radio state, heap info |
| GET | `/api/config` | Current configuration |
| PUT | `/api/config` | Update configuration (JSON body) |
| GET | `/api/mappings` | Current mapping table |
| PUT | `/api/mappings` | Replace mapping table (JSON array) |
| POST | `/api/mappings/reset` | Reset to default mappings |

WebSocket at `/ws` pushes live JSON messages for control changes, radio state, and connection status.

## Project Structure

```
main/
  main.c              Entry point, task orchestration
  usb_dj_host.c/h     USB host driver (14-step vendor init, state parsing)
  tci_client.c/h       TCI WebSocket client (raw TCP + RFC 6455)
  cat_client.c/h       Kenwood CAT TCP client (ZZ extended commands)
  mapping_engine.c/h   Control-to-command mapping with NVS persistence
  config_store.c/h     NVS key-value configuration
  http_server.c/h      HTTP server, REST API, WebSocket, SPIFFS file serving
  wifi_manager.c/h     WiFi STA with AP fallback
  status_led.c/h       WS2812 RGB status LED
  usb_debug.c/h        USB packet debugging and hex dump
frontend/
  src/App.svelte       Tab-based SPA shell
  src/pages/           Dashboard, Mappings, Config, Debug pages
  src/lib/             API client, WebSocket, Svelte stores
```

## Default Mappings

N1-N8 buttons are mapped to FT8 frequencies per band (160m through 6m). Jog wheels control VFO tuning, sliders control volume and drive, and deck buttons handle PTT, mode cycling, mute, and split.

## License

MIT
