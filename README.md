# ESP32 DJ Console

ESP32-S3 firmware that bridges a **Hercules DJ Console MP3 e2** to a **Thetis SDR** via Kenwood CAT (TCP) commands. A built-in web GUI allows live monitoring and configuration.

## What it does

```
[Hercules DJ Console MP3 e2]
        |  USB bulk IN  (38-byte state packets)
        |  USB bulk OUT (3-byte MIDI LED control)
        v
   [ESP32-S3]
        |
        +-- CAT TCP ------> Thetis SDR (tcp://host:31001)
        |   Kenwood ZZ extended commands (328 commands)
        |
        +-- HTTP :80
            +-- Svelte SPA (dashboard, mappings, LEDs, config, debug)
            +-- REST API for configuration
            +-- WebSocket for live control/radio state updates
```

Every knob, slider, button, and jog wheel on the DJ console maps to a radio command. Mappings are fully configurable via the web interface and stored in flash. LED feedback reflects toggle state and button presses.

## Hardware

- **ESP32-S3** dev board (4MB flash minimum)
- **Hercules DJ Console MP3 e2** (VID: 0x06f8, PID: 0xb105)
- External 5V supply for USB VBUS (ESP32 cannot source 5V)

### Wiring

Plug a USB cable from your ESP32-S3 board to the DJ Console. 
For powering you have two options: 5 volt in your board or use the second USB port of your board (if any...)


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

- **Dashboard** - Connection status (USB, CAT), radio state (VFO, mode, TX), heap usage
- **Mappings** - Browse 328 Thetis commands with MIDI-learn, save to flash
- **LEDs** - Visual LED grid, click to toggle/blink, test sweep, all-off
- **Config** - WiFi credentials, CAT host and port, debug level
- **Debug** - Live feed of DJ console control events and CAT traffic

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | System status, radio state, heap info |
| GET | `/api/config` | Current configuration |
| PUT | `/api/config` | Update configuration (JSON body) |
| GET | `/api/commands` | Full command database (328 entries with descriptions) |
| GET | `/api/mappings` | Current mapping table |
| PUT | `/api/mappings` | Replace mapping table (JSON array) |
| POST | `/api/mappings/reset` | Reset to default mappings |
| GET | `/api/leds` | Current LED states |
| POST | `/api/leds` | Set LED (note, velocity) |
| POST | `/api/leds/all-off` | Turn off all LEDs |
| POST | `/api/leds/test` | LED test sweep |

WebSocket at `/ws` pushes live JSON messages for control changes, radio state, LED updates, and connection status.

## Project Structure

```
main/
  main.c              Entry point, task orchestration
  usb_dj_host.c/h     USB host driver (14-step vendor init, bulk IN/OUT)
  cat_client.c/h       Kenwood CAT TCP client (ZZ extended commands)
  mapping_engine.c/h   Control-to-command mapping with 328-command database
  dj_led.c/h           LED driver (MIDI note protocol, set/blink/all-off)
  config_store.c/h     NVS key-value configuration
  http_server.c/h      HTTP server, REST API, WebSocket, LittleFS file serving
  wifi_manager.c/h     WiFi STA with AP fallback and captive portal
  status_led.c/h       WS2812 RGB status LED
  cmd_db_generated.inc Auto-generated command DB (from CATCommands.cs)
frontend/
  src/App.svelte       Tab-based SPA shell
  src/pages/           Dashboard, Mappings, LEDs, Config, Debug pages
  src/lib/             API client, WebSocket, Svelte stores
scripts/
  extract_cat_commands.py  Generator for cmd_db_generated.inc
```

## Default Mappings

N1-N8 buttons are mapped to FT8 frequencies per band (160m through 6m). Jog wheels control VFO tuning, sliders control volume and drive, and deck buttons handle PTT, mode cycling, mute, and split.

## License

MIT
