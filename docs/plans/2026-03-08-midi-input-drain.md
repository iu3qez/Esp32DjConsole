# MIDI Input Drain Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Drain incoming MIDI data from the PC to prevent Windows driver hang.

**Architecture:** New `midi_input` module reads and discards all incoming MIDI bytes each tick of the TinyUSB device task loop. Temporary solution — future work will interpret the data.

**Tech Stack:** ESP-IDF, TinyUSB (`tud_midi_n_stream_read`, `tud_midi_n_available`)

---

### Task 1: Create midi_input header

**Files:**
- Create: `src/midi_input.h`

**Step 1: Write the header**

```c
#pragma once
#include "esp_err.h"

esp_err_t midi_input_init(void);
void midi_input_poll(void);
```

**Step 2: Commit**

```bash
git add src/midi_input.h
git commit -m "Add midi_input.h header"
```

---

### Task 2: Create midi_input implementation

**Files:**
- Create: `src/midi_input.c`

**Step 1: Write the implementation**

```c
#include "midi_input.h"
#include "tusb.h"
#include "esp_log.h"

static const char *TAG = "midi_in";

esp_err_t midi_input_init(void) {
    ESP_LOGI(TAG, "MIDI input ready (drain mode)");
    return ESP_OK;
}

// TEMPORARY: drain all incoming MIDI data to prevent Windows driver hang.
// Data is read and discarded. Future implementation will interpret
// incoming MIDI messages (e.g. for LED feedback control).
void midi_input_poll(void) {
    if (!tud_midi_n_mounted(0)) return;

    uint8_t buf[48];
    while (tud_midi_n_available(0, 0)) {
        tud_midi_n_stream_read(0, 0, buf, sizeof(buf));
    }
}
```

**Step 2: Commit**

```bash
git add src/midi_input.c
git commit -m "Add midi_input.c with drain-only poll"
```

---

### Task 3: Register midi_input in CMakeLists

**Files:**
- Modify: `src/CMakeLists.txt`

**Step 1: Add `midi_input.c` to SRCS**

Change the first line of `idf_component_register` to include `"midi_input.c"` after `"midi_output.c"`:

```
idf_component_register(SRCS "main.c" "usb_descriptors.c" "midi_output.c" "midi_input.c"
```

**Step 2: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "Add midi_input.c to CMakeLists SRCS"
```

---

### Task 4: Wire midi_input into main

**Files:**
- Modify: `src/main.c`

**Step 1: Add include**

After `#include "midi_output.h"` (line 12), add:

```c
#include "midi_input.h"
```

**Step 2: Call midi_input_init()**

After `midi_output_init();` (line 127), add:

```c
    midi_input_init();
```

**Step 3: Call midi_input_poll() in device task loop**

In `tusb_device_task`, after `tud_task_ext(1, false);` (line 80), add:

```c
        midi_input_poll();
```

**Step 4: Build to verify**

```bash
idf.py build
```

Expected: clean build, no errors.

**Step 5: Commit**

```bash
git add src/main.c
git commit -m "Wire midi_input drain into device task loop"
```
