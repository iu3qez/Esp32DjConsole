#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "usb_dj_host.h"

/**
 * Mapping Engine - maps DJ console controls to Thetis CAT commands.
 *
 * Features:
 *   - Auto-generated database of ~300+ Thetis commands (from CATCommands.cs)
 *   - MIDI-learn mode: select command, move control, mapping created
 *   - Mappings saved to SPIFFS as JSON
 *   - Download/upload for backup
 */

// ---------------------------------------------------------------------------
// Thetis command database
// ---------------------------------------------------------------------------

/** How the CAT command is executed when a DJ control changes. */
typedef enum {
    CMD_CAT_BUTTON,   // Send on press only (e.g., ZZBU;)
    CMD_CAT_TOGGLE,   // Track state, send ZZXX0/ZZXX1 on press
    CMD_CAT_SET,      // Knob/slider: scale 0-255 -> value_min..value_max
    CMD_CAT_FREQ,     // Encoder: delta * param Hz, send ZZFA{11-digit freq}
    CMD_CAT_WHEEL,        // Encoder: relative inc/dec via two CAT commands
    CMD_CAT_FILTER_WIDTH, // Knob: set filter width via ZZSF, center tracked from ZZFH/ZZFL
} cmd_exec_type_t;

/** Command categories for UI grouping. */
typedef enum {
    CAT_VFO = 0,
    CAT_BAND,
    CAT_MODE,
    CAT_TX,
    CAT_AUDIO,
    CAT_FILTER,
    CAT_NR_NB,
    CAT_AGC,
    CAT_SPLIT_RIT,
    CAT_CW,
    CAT_MISC,
    CAT_CATEGORY_COUNT,
} cmd_category_t;

/** A single Thetis command definition (ROM, static const). */
typedef struct {
    uint16_t         id;           // Unique ID (stable across firmware versions)
    const char      *name;         // Human-readable: "VFO A Tune", "Band Up"
    const char      *description;  // Full description (NULL if same as name)
    cmd_category_t   category;
    cmd_exec_type_t  exec_type;
    const char      *cat_cmd;      // CAT prefix: "ZZFA", "ZZBU", etc.
    const char      *cat_cmd2;     // Second cmd for WHEEL dec (NULL if unused)
    int              value_digits;  // 0=no param, 1-11=zero-padded digits
    int              value_min;
    int              value_max;
} thetis_cmd_t;

/** Get the full command database array and its size. */
const thetis_cmd_t *cmd_db_get_all(int *count);

/** Look up a command by ID. Returns NULL if not found. */
const thetis_cmd_t *cmd_db_find(uint16_t id);

/** Get human-readable category name. */
const char *cmd_category_name(cmd_category_t cat);

// ---------------------------------------------------------------------------
// Mapping entries
// ---------------------------------------------------------------------------

/** A single control-to-command mapping. */
typedef struct {
    char     control_name[24];  // DJ control: "Jog_A", "Play_A", etc.
    uint16_t command_id;        // Thetis command ID from database
    int32_t  param;             // Step size (Hz for VFO), or 0 for default
} mapping_entry_t;

#define MAX_MAPPINGS 64

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/** Initialize: load mappings from SPIFFS, fall back to defaults. */
esp_err_t mapping_engine_init(void);

/** DJ control change callback - dispatches CAT command per mapping. */
void mapping_engine_on_control(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value);

/** Get the current mapping table (read-only). */
const mapping_entry_t *mapping_engine_get_table(int *count);

/** Set a mapping entry by control name. Overwrites if exists, appends if new. */
esp_err_t mapping_engine_set(const mapping_entry_t *entry);

/** Remove a mapping by control name. */
esp_err_t mapping_engine_remove(const char *control_name);

/** Save current mappings to SPIFFS. */
esp_err_t mapping_engine_save(void);

/** Load mappings from SPIFFS. Returns ESP_ERR_NOT_FOUND if no file. */
esp_err_t mapping_engine_load(void);

/** Reset to default mappings and save. */
void mapping_engine_reset_defaults(void);

// ---------------------------------------------------------------------------
// MIDI Learn mode
// ---------------------------------------------------------------------------

/** Start learn mode: next DJ control change creates a mapping to command_id. */
void mapping_engine_start_learn(uint16_t command_id);

/** Check if learn mode is active. */
bool mapping_engine_is_learning(void);

/** Cancel learn mode without creating a mapping. */
void mapping_engine_cancel_learn(void);

/**
 * Callback fired when learn mode completes (a control was moved).
 * Set via mapping_engine_set_learn_callback().
 */
typedef void (*mapping_learn_callback_t)(
    const char *control_name,
    uint16_t command_id,
    const char *command_name);

void mapping_engine_set_learn_callback(mapping_learn_callback_t cb);

/**
 * Callback fired when a CAT command is dispatched (for debug logging).
 * Set via mapping_engine_set_cat_callback().
 */
typedef void (*mapping_cat_callback_t)(
    const char *control_name,
    const char *command_name,
    cmd_exec_type_t exec_type,
    const char *cat_string);

void mapping_engine_set_cat_callback(mapping_cat_callback_t cb);

// ---------------------------------------------------------------------------
// CAT response sync — feed Thetis responses to keep VFO/step in sync
// ---------------------------------------------------------------------------

/** Handle CAT response from Thetis (updates local VFO freq and tuning step). */
void mapping_engine_on_cat_response(const char *cmd, const char *value);

/** Query Thetis for current ZZFA, ZZFB, ZZAC. Call after CAT connects. */
void mapping_engine_request_sync(void);
