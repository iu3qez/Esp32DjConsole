---
name: build-flash
description: Build and flash ESP32 DJ Console firmware with frontend
---

# Build & Flash ESP32 DJ Console

## Environment Setup

ESP-IDF must be sourced before any build commands:

```bash
source ~/esp/esp-idf/export.sh
```

## Build Commands

### Full build (frontend + firmware)

Use the unified build script:

```bash
cd /home/sf/src/Esp32DjConsole
./build.sh
```

This runs: `npm install` (if needed) -> `npm run build` -> gzip assets -> `idf.py build` (includes SPIFFS image).

### Build and flash

```bash
./build.sh flash              # Auto-detect port
./build.sh flash /dev/ttyUSB0 # Specific port
```

### Frontend only

```bash
cd frontend && npm run build
```

Output goes to `build/www/`. The SPIFFS image is regenerated on next `idf.py build`.

### Firmware only (skip frontend rebuild)

```bash
idf.py build
```

### Flash only (already built)

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Partition Layout

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| nvs | 0x9000 | 24KB | Config (WiFi creds, settings) |
| phy_init | 0xF000 | 4KB | PHY calibration |
| factory | 0x10000 | 1.875MB | Firmware |
| www | 0x1F0000 | 1MB | SPIFFS (frontend assets) |

## Key Files

- `sdkconfig.defaults` — ESP-IDF config overrides (edit this, NOT `sdkconfig`)
- `sdkconfig` — auto-generated, do NOT edit directly
- `partitions.csv` — partition table
- `CMakeLists.txt` — builds frontend via npm, generates SPIFFS image
- `build.sh` — unified build script with gzip compression

## Common Issues

- **"ESP-IDF not found"**: Run `source ~/esp/esp-idf/export.sh` first
- **Frontend changes not showing**: Ensure `build/www/` has fresh files, then `idf.py build` to regenerate SPIFFS
- **Partition too small**: www partition is 1MB, check `build/www/` total size
- **sdkconfig conflicts**: Delete `sdkconfig` and rebuild — `sdkconfig.defaults` will regenerate it
