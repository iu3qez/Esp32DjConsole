#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "usb_dj_host.h"

/**
 * Mapping Engine - translates DJ console control events to TCI/CAT commands.
 *
 * Each DJ control (button, dial, encoder) maps to an action that generates
 * one or more TCI or CAT commands. The mapping table is stored in NVS as JSON
 * and can be edited via the web GUI.
 */

/**
 * Action types for DJ control mappings.
 */
typedef enum {
    MAP_ACTION_NONE = 0,       // Unmapped (ignore)

    // Frequency
    MAP_ACTION_VFO_A_STEP,     // Encoder/dial -> VFO A freq step (param: Hz per tick)
    MAP_ACTION_VFO_B_STEP,     // Encoder/dial -> VFO B freq step
    MAP_ACTION_VFO_A_DIRECT,   // Dial -> VFO A frequency (scaled to band)
    MAP_ACTION_BAND_SELECT,    // Button -> jump to band preset (param: freq in Hz)

    // Mode
    MAP_ACTION_MODE_SET,       // Button -> set specific mode (param: mode string)
    MAP_ACTION_MODE_CYCLE,     // Button -> cycle through mode list

    // Audio
    MAP_ACTION_VOLUME,         // Dial -> volume (0-100 linear from 0x00-0xFF)
    MAP_ACTION_VOLUME_STEP,    // Encoder -> volume step (param: step size)

    // TX
    MAP_ACTION_PTT_TOGGLE,     // Button -> toggle TX on/off
    MAP_ACTION_PTT_MOMENTARY,  // Button -> TX while pressed
    MAP_ACTION_TUNE_TOGGLE,    // Button -> toggle tune
    MAP_ACTION_DRIVE,          // Dial -> TX drive (0-100)
    MAP_ACTION_DRIVE_STEP,     // Encoder -> drive step

    // Filter/EQ
    MAP_ACTION_FILTER_WIDTH,   // Dial -> filter bandwidth (scaled)
    MAP_ACTION_FILTER_SHIFT,   // Encoder -> filter shift

    // Misc
    MAP_ACTION_MUTE_TOGGLE,    // Button -> toggle mute
    MAP_ACTION_SPLIT_TOGGLE,   // Button -> toggle split

    // Custom
    MAP_ACTION_CUSTOM_TCI,     // Send arbitrary TCI command (param: command template)
    MAP_ACTION_CUSTOM_CAT,     // Send arbitrary CAT command (param: command template)

    MAP_ACTION_COUNT
} mapping_action_t;

/**
 * A single control-to-action mapping entry.
 */
typedef struct {
    char               control_name[24]; // DJ control name (e.g., "Jog_A", "Play_A")
    mapping_action_t   action;
    int32_t            param_int;        // Integer parameter (Hz step, band freq, mode code)
    char               param_str[32];    // String parameter (mode name, custom command template)
    uint8_t            rx;               // Target receiver (0 or 1)
} mapping_entry_t;

#define MAX_MAPPINGS 64

/**
 * Initialize the mapping engine with default mappings.
 * Call after usb_dj_host_init() and tci/cat client init.
 */
esp_err_t mapping_engine_init(void);

/**
 * DJ control change callback - connect this to usb_dj_host_init().
 * Looks up the mapping and dispatches the appropriate TCI/CAT command.
 */
void mapping_engine_on_control(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value);

/**
 * Get the current mapping table (read-only).
 */
const mapping_entry_t *mapping_engine_get_table(int *count);

/**
 * Set a mapping entry by control name. Overwrites if exists, appends if new.
 * Returns ESP_ERR_NO_MEM if table is full.
 */
esp_err_t mapping_engine_set(const mapping_entry_t *entry);

/**
 * Remove a mapping by control name.
 */
esp_err_t mapping_engine_remove(const char *control_name);

/**
 * Save current mappings to NVS.
 */
esp_err_t mapping_engine_save(void);

/**
 * Load mappings from NVS (replaces current table).
 * Returns ESP_ERR_NOT_FOUND if no saved mappings exist (defaults remain).
 */
esp_err_t mapping_engine_load(void);

/**
 * Reset to default mappings (does not save to NVS).
 */
void mapping_engine_reset_defaults(void);

/**
 * Set which protocol to use for dispatching commands.
 * @param use_tci  true = TCI (WebSocket), false = CAT (TCP)
 */
void mapping_engine_set_protocol(bool use_tci);
bool mapping_engine_get_protocol(void);
