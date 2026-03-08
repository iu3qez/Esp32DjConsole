# MIDI Input Drain

**Date:** 2026-03-08
**Branch:** esp32p4-midi-bridge

## Problem

The ESP32-P4 presents itself as a USB MIDI device to the PC (TinyUSB device on rhport 0).
The MIDI OUT endpoint (0x01) is declared in the USB descriptor, so the PC/Windows driver
can send MIDI messages to the ESP32. However, nobody reads these messages. The TinyUSB
buffer fills up and the Windows driver hangs.

## Solution

Drain all incoming MIDI data without interpreting it. This is a temporary solution;
future work will add proper MIDI input handling.

## Changes

1. **New `src/midi_input.h`** — declares `midi_input_init()` and `midi_input_poll()`
2. **New `src/midi_input.c`** — implements:
   - `midi_input_init()` — log ready
   - `midi_input_poll()` — calls `tud_midi_n_stream_read()` in a loop until buffer
     is empty, discards all data. Commented as temporary drain solution.
3. **`src/main.c`** — includes `midi_input.h`, calls `midi_input_init()` after
   `midi_output_init()`, calls `midi_input_poll()` in `tusb_device_task` loop
   after `tud_task_ext()`
4. **`src/CMakeLists.txt`** — adds `midi_input.c` to SRCS

## What doesn't change

- `midi_output.c` — untouched
- `event_engine.c` — untouched
- `usb_descriptors.c` — endpoint OUT 0x01 already declared in MIDI descriptor
