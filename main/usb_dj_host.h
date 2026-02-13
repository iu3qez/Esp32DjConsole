#pragma once

#include <stdint.h>
#include "esp_err.h"

#define HERCULES_VID    0x06F8
#define HERCULES_PID    0xB105

#define DJ_STATE_SIZE   38

/**
 * Control types matching the original Teensy driver.
 */
typedef enum {
    DJ_CTRL_BUTTON = 0,   // On/off, bitmask yields 0 or 1
    DJ_CTRL_DIAL   = 1,   // Continuous 0x00-0xFF (sliders, knobs)
    DJ_CTRL_ENCODER = 2,  // Rotary encoder 0x00-0xFF with wrap-around (jog wheels)
} dj_control_type_t;

/**
 * Callback fired when a DJ console control changes state.
 *
 * @param name          Control name (e.g., "Play_A", "Jog_A", "Vol_A")
 * @param control_type  Button, dial, or encoder
 * @param control_index Index in the mappings table (0-based)
 * @param old_value     Previous value
 * @param new_value     New value
 */
typedef void (*dj_control_callback_t)(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value);

/**
 * Initialize the USB host stack and start scanning for the Hercules DJ Console.
 * Spawns two FreeRTOS tasks: usb_lib_task and usb_client_task.
 *
 * @param callback  Function called on each control state change (may be NULL)
 * @return ESP_OK on success
 */
esp_err_t usb_dj_host_init(dj_control_callback_t callback);

/**
 * Returns true if the DJ console is currently connected and initialized.
 */
bool usb_dj_host_is_connected(void);

/**
 * Get a pointer to the current 38-byte device state (read-only).
 * Returns NULL if device is not connected.
 */
const uint8_t *usb_dj_host_get_state(void);

/**
 * Optional callback for raw state updates (called before diffing).
 * Useful for debug hex dumping of every incoming packet.
 *
 * @param raw_data    Pointer to the 38-byte raw state just received
 * @param length      Number of bytes received
 */
typedef void (*dj_raw_state_callback_t)(const uint8_t *raw_data, int length);

/**
 * Register a raw state callback for debug/monitoring.
 */
void usb_dj_host_set_raw_callback(dj_raw_state_callback_t cb);
