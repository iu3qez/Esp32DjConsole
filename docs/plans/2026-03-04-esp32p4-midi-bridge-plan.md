# ESP32-P4 USB MIDI Bridge — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create an ESP32-P4 firmware that reads USB input from a Hercules DJ Console (Host port) and re-emits it as standard USB MIDI (Device port), with a WiFi web GUI for mapping configuration.

**Architecture:** Layered bridge — USB Host reads proprietary 38-byte packets, translates to generic control events, applies configurable mappings, outputs USB MIDI via TinyUSB Device class. WiFi + Svelte web GUI for mapping CRUD. Optional CAT output preserved.

**Tech Stack:** ESP-IDF v5.5, TinyUSB fork (dual Host+Device on ESP32-P4), FreeRTOS, Svelte, NVS

---

## Task 0: Study the TinyUSB Fork

**Goal:** Understand the dual Host+Device setup before writing any code.

**Step 1: Read the fork's example**

Study these files in `/home/sf/src/tinyusb/examples_esp32p4/host_device_cdc/`:
- `CMakeLists.txt` — how to reference TinyUSB as ESP-IDF component, board config, rhport assignments
- `src/tusb_config.h` — `CFG_TUD_ENABLED=1`, `CFG_TUH_ENABLED=1`, device on rhport 0 (FS), host on rhport 1 (HS)
- `src/main.c` — `usb_phy_dual_init()` pattern: LP_SYS PHY mux swap, FS PHY (device), HS PHY (host), then `tud_rhport_init(0)` + `tuh_rhport_init(1)`
- `src/usb_descriptors.c` — how device descriptors are declared for TinyUSB

**Step 2: Note the key patterns**

- PHY mux swap: `LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1; LP_SYS.usb_ctrl.sw_usb_phy_sel = 1;`
- FS PHY = `USB_PHY_CTRL_OTG` + `USB_PHY_TARGET_INT` + `USB_OTG_MODE_DEVICE`
- HS PHY = `USB_PHY_CTRL_OTG` + `USB_PHY_TARGET_UTMI` + `USB_OTG_MODE_HOST`
- Two FreeRTOS tasks: `tud_task_ext(1, false)` and `tuh_task_ext(1, false)`
- Host uses `tuh_edpt_open()` + `tuh_edpt_xfer()` for vendor bulk endpoints
- `CFG_TUH_API_EDPT_XFER = 1` enables user endpoint transfer callbacks

**Step 3: Check the dual_device_cdc example too**

Skim `/home/sf/src/tinyusb/examples_esp32p4/dual_device_cdc/` to understand if there are additional patterns.

**Step 4: Verify the fork builds**

```bash
cd /home/sf/src/tinyusb/examples_esp32p4/host_device_cdc
source ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
```

Expected: successful build (no need to flash yet).

---

## Task 1: Create Branch and Project Scaffold

**Files:**
- Create: `CMakeLists.txt` (new, P4-specific top-level)
- Create: `partitions_p4.csv`
- Create: `sdkconfig.defaults.esp32p4`
- Create: `main_p4/CMakeLists.txt`
- Create: `main_p4/main.c`
- Create: `main_p4/tusb_config.h`

**Step 1: Create orphan branch**

```bash
git checkout --orphan esp32p4-midi-bridge
git rm -rf .
```

Or alternatively, create a regular branch and restructure:

```bash
git checkout -b esp32p4-midi-bridge master
```

**Step 2: Create top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32p4")

# TinyUSB fork (adjust path as needed)
set(TUSB_ROOT ${CMAKE_CURRENT_LIST_DIR}/../tinyusb)
set(TUSB_BSP ${TUSB_ROOT}/hw/bsp/espressif)

set(BOARD espressif_p4_function_ev)
include(${TUSB_BSP}/boards/${BOARD}/board.cmake)

# RHPort: port 0 = FS device (MIDI), port 1 = HS host (DJ Console)
set(RHPORT_DEVICE 0)
set(RHPORT_DEVICE_SPEED OPT_MODE_FULL_SPEED)
set(RHPORT_HOST 1)
set(RHPORT_HOST_SPEED OPT_MODE_HIGH_SPEED)

set(EXTRA_COMPONENT_DIRS
  "main_p4"
  "${TUSB_BSP}/boards"
  "${TUSB_BSP}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32p4_midi_bridge)
```

**Step 3: Create partition table `partitions_p4.csv`**

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
phy_init, data, phy,     0xf000,  0x1000
factory,  app,  factory, 0x10000, 0x1E0000
www,      data, spiffs,  0x1F0000,0x10000
```

**Step 4: Create minimal `main_p4/main.c`**

Skeleton `app_main()` that only initializes dual USB PHYs and prints "Hello MIDI Bridge".

```c
#include <stdio.h>
#include "esp_log.h"
#include "tusb.h"
#include "esp_private/usb_phy.h"
#include "soc/lp_system_struct.h"

static const char *TAG = "midi_bridge";

static usb_phy_handle_t fs_phy_hdl;
static usb_phy_handle_t hs_phy_hdl;

static bool usb_phy_dual_init(void) {
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1;
    LP_SYS.usb_ctrl.sw_usb_phy_sel = 1;

    usb_phy_config_t fs_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_INT,
        .otg_mode   = USB_OTG_MODE_DEVICE,
        .otg_speed  = USB_PHY_SPEED_FULL,
    };
    if (usb_new_phy(&fs_conf, &fs_phy_hdl) != ESP_OK) return false;

    usb_phy_config_t hs_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_UTMI,
        .otg_mode   = USB_OTG_MODE_HOST,
        .otg_speed  = USB_PHY_SPEED_HIGH,
    };
    if (usb_new_phy(&hs_conf, &hs_phy_hdl) != ESP_OK) return false;

    return true;
}

void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32-P4 MIDI Bridge ===");

    if (!usb_phy_dual_init()) {
        ESP_LOGE(TAG, "USB PHY init failed");
        return;
    }
    ESP_LOGI(TAG, "Dual USB PHY initialized");
}
```

**Step 5: Create `main_p4/tusb_config.h`**

```c
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// Device on rhport 0 (FS) — MIDI class
#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_MIDI          1
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

// Host on rhport 1 (HS) — vendor bulk for DJ Console
#define CFG_TUH_ENABLED       1
#define CFG_TUH_MAX_SPEED     OPT_MODE_HIGH_SPEED
#define CFG_TUH_API_EDPT_XFER 1
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB           0
#define CFG_TUH_DEVICE_MAX    1

#endif
```

**Step 6: Create `main_p4/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "main.c" "usb_descriptors.c"
                    INCLUDE_DIRS "."
                    REQUIRES boards tinyusb_src
                    PRIV_REQUIRES usb nvs_flash)
```

**Step 7: Build to verify scaffold**

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
```

**Step 8: Commit**

```bash
git add -A
git commit -m "Scaffold ESP32-P4 MIDI bridge with dual USB PHY init"
```

---

## Task 2: USB MIDI Device Descriptors

**Files:**
- Create: `main_p4/usb_descriptors.c`

**Step 1: Write MIDI USB descriptors**

TinyUSB provides `TUD_MIDI_DESC_HEAD` and `TUD_MIDI_DESC_JACK` macros. The device will appear as a standard USB MIDI device with 1 IN and 1 OUT jack.

```c
#include "tusb.h"
#include "esp_mac.h"

#define USB_VID   0xCafe
#define USB_PID   0x4001
#define USB_BCD   0x0200

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*) &desc_device;
}

enum { ITF_NUM_MIDI = 0, ITF_NUM_MIDI_STREAMING, ITF_NUM_TOTAL };

#define EPNUM_MIDI_OUT  0x01
#define EPNUM_MIDI_IN   0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

// String descriptors (Manufacturer, Product, Serial)
// ... (standard pattern with esp_efuse_mac for serial)
```

**Step 2: Build and verify**

```bash
idf.py build
```

**Step 3: Commit**

```bash
git add main_p4/usb_descriptors.c
git commit -m "Add USB MIDI device descriptors"
```

---

## Task 3: MIDI Output Module

**Files:**
- Create: `main_p4/midi_output.h`
- Create: `main_p4/midi_output.c`

**Step 1: Define the MIDI output API**

```c
// midi_output.h
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    MIDI_NOTE_OFF     = 0x80,
    MIDI_NOTE_ON      = 0x90,
    MIDI_CC           = 0xB0,
    MIDI_PROG_CHANGE  = 0xC0,
} midi_msg_type_t;

esp_err_t midi_output_init(void);
esp_err_t midi_output_send_cc(uint8_t channel, uint8_t cc_num, uint8_t value);
esp_err_t midi_output_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
esp_err_t midi_output_send_note_off(uint8_t channel, uint8_t note);
```

**Step 2: Implement using TinyUSB MIDI Device API**

```c
// midi_output.c
#include "midi_output.h"
#include "tusb.h"
#include "esp_log.h"

static const char *TAG = "midi_out";

esp_err_t midi_output_init(void) {
    // TinyUSB device init happens in main, this just validates
    ESP_LOGI(TAG, "MIDI output ready");
    return ESP_OK;
}

esp_err_t midi_output_send_cc(uint8_t channel, uint8_t cc_num, uint8_t value) {
    uint8_t msg[3] = { (uint8_t)(MIDI_CC | (channel & 0x0F)), cc_num & 0x7F, value & 0x7F };
    if (!tud_midi_n_mounted(0)) return ESP_ERR_INVALID_STATE;
    return tud_midi_n_stream_write(0, 0, msg, 3) ? ESP_OK : ESP_FAIL;
}

esp_err_t midi_output_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    uint8_t msg[3] = { (uint8_t)(MIDI_NOTE_ON | (channel & 0x0F)), note & 0x7F, velocity & 0x7F };
    if (!tud_midi_n_mounted(0)) return ESP_ERR_INVALID_STATE;
    return tud_midi_n_stream_write(0, 0, msg, 3) ? ESP_OK : ESP_FAIL;
}

esp_err_t midi_output_send_note_off(uint8_t channel, uint8_t note) {
    uint8_t msg[3] = { (uint8_t)(MIDI_NOTE_OFF | (channel & 0x0F)), note & 0x7F, 0 };
    if (!tud_midi_n_mounted(0)) return ESP_ERR_INVALID_STATE;
    return tud_midi_n_stream_write(0, 0, msg, 3) ? ESP_OK : ESP_FAIL;
}
```

**Step 3: Build**

```bash
idf.py build
```

**Step 4: Commit**

```bash
git add main_p4/midi_output.c main_p4/midi_output.h
git commit -m "Add MIDI output module using TinyUSB Device MIDI class"
```

---

## Task 4: Port USB Host Driver for TinyUSB

**Files:**
- Create: `main_p4/usb_dj_host.c`
- Create: `main_p4/usb_dj_host.h`

**Goal:** Adapt `main/usb_dj_host.c` from ESP-IDF USB Host Library API to TinyUSB Host API (`tuh_edpt_open`, `tuh_edpt_xfer`, vendor control transfers).

**Step 1: Define the same callback API as current project**

Copy `main/usb_dj_host.h` — the `control_event_t` types and callback signature stay the same.

**Step 2: Rewrite internals using TinyUSB Host API**

Key changes from current ESP-IDF USB Host lib:
- Replace `usb_host_client_register()` with TinyUSB `tuh_mount_cb()` / `tuh_umount_cb()` callbacks
- Replace `usb_host_transfer_submit()` with `tuh_edpt_xfer()`
- The 14-step vendor init sequence uses `tuh_control_xfer()` instead of `usb_host_transfer_submit()` with control endpoint
- Bulk IN reading uses `tuh_edpt_open()` + `tuh_edpt_xfer()` with callback (same pattern as `host_device_cdc` example)
- State diffing logic (38-byte comparison, callback dispatch) stays identical

**Step 3: Implement the DJ Console vendor init sequence**

Port the 14 control transfers from `sample.ino` / current `usb_dj_host.c` to TinyUSB `tuh_control_xfer()`.

**Step 4: Build and verify**

```bash
idf.py build
```

**Step 5: Commit**

```bash
git add main_p4/usb_dj_host.c main_p4/usb_dj_host.h
git commit -m "Port USB host driver for DJ Console to TinyUSB Host API"
```

---

## Task 5: Event Engine (Generic Events + MIDI Mapping)

**Files:**
- Create: `main_p4/event_engine.h`
- Create: `main_p4/event_engine.c`

**Step 1: Define generic event types**

```c
// event_engine.h
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    CTRL_BUTTON,
    CTRL_SLIDER,
    CTRL_ENCODER,
} control_type_t;

typedef struct {
    uint8_t control_id;
    control_type_t type;
    int16_t value;
} control_event_t;

typedef struct {
    uint8_t control_id;
    uint8_t midi_channel;
    uint8_t midi_type;      // MIDI_CC, MIDI_NOTE_ON, etc.
    uint8_t midi_param;     // CC number or note number
} midi_mapping_t;

#define MAX_MIDI_MAPPINGS 64

esp_err_t event_engine_init(void);
void event_engine_process(const control_event_t *event);
const midi_mapping_t *event_engine_get_mappings(int *count);
esp_err_t event_engine_set_mapping(const midi_mapping_t *mapping);
esp_err_t event_engine_remove_mapping(uint8_t control_id);
esp_err_t event_engine_save(void);
esp_err_t event_engine_load(void);
```

**Step 2: Implement**

- `event_engine_process()`: look up `control_id` in mapping table, call `midi_output_send_cc()` / `midi_output_send_note_on()` etc.
- Buttons → Note On (press) / Note Off (release), or CC toggle
- Sliders → CC with scaled value
- Encoders → CC with relative value (increment/decrement mapped to 0x01-0x3F / 0x41-0x7F)
- Persistence: JSON in NVS via `config_store.c` (port from current project)

**Step 3: Build**

```bash
idf.py build
```

**Step 4: Commit**

```bash
git add main_p4/event_engine.c main_p4/event_engine.h
git commit -m "Add event engine with configurable MIDI mappings"
```

---

## Task 6: Wire Everything Together in main.c

**Files:**
- Modify: `main_p4/main.c`

**Step 1: Complete app_main**

```c
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32-P4 MIDI Bridge ===");

    // NVS init
    nvs_flash_init();

    // Dual USB PHY init
    usb_phy_dual_init();

    // TinyUSB Device init (rhport 0, FS, MIDI)
    tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
    tud_rhport_init(0, &dev_init);

    // TinyUSB Host init (rhport 1, HS)
    tusb_rhport_init_t host_init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_HIGH };
    tuh_rhport_init(1, &host_init);

    // Init modules
    midi_output_init();
    event_engine_init();  // loads mappings from NVS
    usb_dj_host_init(on_dj_control);

    // FreeRTOS tasks
    xTaskCreatePinnedToCore(tusb_device_task, "usbd", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(tusb_host_task, "usbh", 4096, NULL, 5, NULL, 1);
}

// DJ control callback → event engine
static void on_dj_control(const char *name, dj_control_type_t type,
                           uint8_t index, uint8_t old_val, uint8_t new_val) {
    control_event_t evt = {
        .control_id = index,
        .type = (control_type_t)type,
        .value = (type == DJ_CTRL_ENCODER) ? (int16_t)(int8_t)(new_val - old_val) : new_val,
    };
    event_engine_process(&evt);
}
```

**Step 2: Build and verify**

```bash
idf.py build
```

**Step 3: Commit**

```bash
git add main_p4/main.c
git commit -m "Wire USB host, event engine, and MIDI output together"
```

---

## Task 7: Port Config Store and WiFi Manager

**Files:**
- Copy+adapt: `main/config_store.c` → `main_p4/config_store.c`
- Copy+adapt: `main/config_store.h` → `main_p4/config_store.h`
- Copy+adapt: `main/wifi_manager.c` → `main_p4/wifi_manager.c`
- Copy+adapt: `main/wifi_manager.h` → `main_p4/wifi_manager.h`

**Step 1: Copy files**

These should need minimal changes — they use ESP-IDF NVS/WiFi APIs which are the same on P4.

**Step 2: Remove CAT-specific config keys, add MIDI-specific ones if needed**

**Step 3: Build and commit**

```bash
git add main_p4/config_store.* main_p4/wifi_manager.*
git commit -m "Port config store and WiFi manager from S3 project"
```

---

## Task 8: Port HTTP Server and Web GUI

**Files:**
- Copy+adapt: `main/http_server.c` → `main_p4/http_server.c`
- Copy+adapt: `main/http_server.h` → `main_p4/http_server.h`
- Copy+adapt: `frontend/` → `frontend_p4/` (or reuse)

**Step 1: Adapt REST API**

- `/api/mappings` now returns/accepts MIDI mappings (channel, type, param) instead of CAT command IDs
- `/api/commands` replaced with `/api/midi-params` (list of CC names, note names)
- `/api/status` adapted for MIDI device status

**Step 2: Adapt frontend**

- Mapping page: dropdowns for MIDI channel (1-16), message type (CC, Note, Program Change), param number
- Remove CAT command browser, replace with MIDI parameter picker
- Dashboard: show MIDI device mounted status instead of CAT connection

**Step 3: Build frontend and firmware**

```bash
cd frontend_p4 && npm install && npm run build && cd ..
idf.py build
```

**Step 4: Commit**

```bash
git add main_p4/http_server.* frontend_p4/
git commit -m "Port HTTP server and Svelte GUI for MIDI mapping configuration"
```

---

## Task 9: Integration Test on Hardware

**Step 1: Flash to ESP32-P4**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

**Step 2: Verify USB MIDI device appears on PC**

- Linux: `aconnect -l` or `amidi -l` should show the device
- Windows: Device Manager → Sound, video and game controllers

**Step 3: Connect DJ Console to Host port**

Verify control events in serial log.

**Step 4: Test MIDI output**

Use a MIDI monitor (e.g., `aseqdump` on Linux, MIDI-OX on Windows) to verify CC/Note messages arrive when moving DJ Console controls.

**Step 5: Test web GUI**

Connect to WiFi, open `http://djconsole.local`, verify mapping page works.

**Step 6: Commit any fixes**

```bash
git commit -m "Fix integration issues found during hardware testing"
```

---

## Task Summary

| # | Task | New/Port | Key Files |
|---|------|----------|-----------|
| 0 | Study TinyUSB fork | Research | (read-only) |
| 1 | Branch + scaffold | New | `CMakeLists.txt`, `main_p4/main.c`, `tusb_config.h` |
| 2 | MIDI device descriptors | New | `main_p4/usb_descriptors.c` |
| 3 | MIDI output module | New | `main_p4/midi_output.c/h` |
| 4 | Port USB host driver | Port | `main_p4/usb_dj_host.c/h` |
| 5 | Event engine | New | `main_p4/event_engine.c/h` |
| 6 | Wire main.c | New | `main_p4/main.c` |
| 7 | Port config + WiFi | Port | `main_p4/config_store.*`, `wifi_manager.*` |
| 8 | Port HTTP + web GUI | Port | `main_p4/http_server.*`, `frontend_p4/` |
| 9 | Hardware integration test | Test | — |
