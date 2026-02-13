#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "usb_dj_host.h"

/**
 * USB Debug Module
 *
 * Provides raw hex dump logging, control change logging, and a
 * state snapshot for the web debug interface. Since the sample.ino
 * mappings may not be 100% correct, this module helps identify
 * which bytes change when controls are manipulated.
 */

/**
 * Set debug verbosity level.
 *   0 = off (only errors)
 *   1 = control changes only (name + value)
 *   2 = control changes + raw hex dump of changed bytes
 *   3 = full 38-byte hex dump on every state change
 */
void usb_debug_set_level(int level);
int  usb_debug_get_level(void);

/**
 * Debug callback suitable for usb_dj_host_init().
 * Logs control changes according to the current debug level.
 */
void usb_debug_control_cb(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value);

/**
 * Log a full hex dump of the current 38-byte state buffer.
 * Call manually or from serial console command.
 */
void usb_debug_dump_state(void);

/**
 * Log a side-by-side diff of two state buffers, highlighting changed bytes.
 */
void usb_debug_diff_states(const uint8_t *old_state, const uint8_t *new_state);

/**
 * Get total number of state updates received since last reset.
 */
uint32_t usb_debug_get_update_count(void);

/**
 * Get total number of control change events since last reset.
 */
uint32_t usb_debug_get_change_count(void);

/**
 * Reset debug counters.
 */
void usb_debug_reset_counters(void);

/**
 * Raw state callback suitable for usb_dj_host_set_raw_callback().
 * Tracks packet counts and optionally dumps raw hex at level 3.
 */
void usb_debug_raw_state_cb(const uint8_t *raw_data, int length);
