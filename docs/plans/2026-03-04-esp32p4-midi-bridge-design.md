# ESP32-P4 USB MIDI Bridge — Design Document

**Date:** 2026-03-04
**Status:** Approved
**Branch:** TBD (independent branch in Esp32DjConsole repo)

## Vision

A "man in the middle" USB MIDI bridge on ESP32-P4. Sits between any USB controller (starting with Hercules DJ Console MP3 e2) and a PC, translating proprietary USB packets into standard MIDI USB messages. Configurable via WiFi web GUI. Future: bidirectional MIDI interception/manipulation.

## Architecture — Layered Bridge

```
[DJ Console] --USB Host--> [ESP32-P4] --USB Device MIDI--> [PC]
                                |
                           WiFi + Web GUI (config)
                           CAT output (optional)
```

### Layer 1: Input Driver
- USB Host via TinyUSB fork (dual Host+Device support on P4)
- Reads 38-byte proprietary bulk IN packets from DJ Console
- Translates to generic `control_event_t` structs
- Ported from `usb_dj_host.c`, adapted for TinyUSB P4

### Layer 2: Event Engine
- Receives `control_event_t` from FreeRTOS queue
- Applies configurable mappings (`midi_mapping_t`)
- Produces MIDI messages (and optionally CAT commands)
- New module: `event_engine.c/h`

### Layer 3: MIDI Output
- TinyUSB USB Device MIDI class
- Emits standard MIDI: Note On/Off, CC, Program Change, etc.
- PC sees a standard USB MIDI controller
- New module: `midi_output.c/h`

### Layer 4: Web GUI + Config
- WiFi STA mode, HTTP server with Svelte SPA
- REST API for mapping CRUD
- WebSocket for live event monitoring
- NVS persistence for mappings
- Reused from current project with adaptations

## Data Structures

### Generic Control Event
```c
typedef enum {
    CTRL_BUTTON,    // on/off, momentary or toggle
    CTRL_SLIDER,    // 0x00-0xFF absolute
    CTRL_ENCODER,   // relative delta (+/-)
} control_type_t;

typedef struct {
    uint8_t control_id;     // unique control ID (0-127)
    control_type_t type;
    int16_t value;          // button: 0/1, slider: 0-255, encoder: delta
} control_event_t;
```

### MIDI Mapping
```c
typedef struct {
    uint8_t control_id;         // source control
    uint8_t midi_channel;       // 0-15
    uint8_t midi_type;          // NOTE_ON, CC, PROGRAM_CHANGE...
    uint8_t midi_param;         // note number or CC number
} midi_mapping_t;
```

## FreeRTOS Tasks

| Task | Core | Priority | Role |
|------|------|----------|------|
| `usb_host_task` | 0 | High | Read DJ Console, produce events |
| `event_engine_task` | 1 | High | Apply mappings, emit MIDI |
| `http_server_task` | 0 | Low | Web GUI, REST API, WebSocket |

**Communication:** FreeRTOS Queue (`event_queue`) between input and engine. Mapping table in RAM protected by mutex, persisted to NVS on change.

## Code Reuse Plan

### Reuse/adapt from current project (~60%)
- `usb_dj_host.c/h` — USB Host driver, adapted for TinyUSB P4 API
- `mapping_engine.c/h` — base structure, rewritten for MIDI output
- `config_store.c/h` — NVS persistence, near-direct reuse
- `http_server.c/h` — web server + API, adapted endpoints
- `wifi_manager.c/h` — direct reuse
- `frontend/` — Svelte SPA, mapping page shows MIDI CC/Note instead of CAT commands
- `dj_led.c/h` — LED feedback

### New code (~40%)
- `midi_output.c/h` — TinyUSB USB Device MIDI class
- `event_engine.c/h` — new core connecting input → mapping → MIDI output
- CMake/sdkconfig setup for ESP32-P4
- TinyUSB fork integration (dual Host + Device)

### Optional/legacy
- `cat_client.c/h` — kept as alternative output, not primary focus
- `cmd_db_generated.inc` — CAT/Thetis specific, not needed for MIDI

## Future Extensions
- Bidirectional MIDI MITM (intercept PC → controller too)
- Support for additional USB controllers (generic HID parsing)
- MIDI filtering/transformation rules
- BLE MIDI output option
