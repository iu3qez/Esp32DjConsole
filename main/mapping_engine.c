#include "mapping_engine.h"
#include "config_store.h"
#include "tci_client.h"
#include "cat_client.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "mapper";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static mapping_entry_t s_mappings[MAX_MAPPINGS];
static int s_mapping_count = 0;
static bool s_use_tci = true;  // Default to TCI

// PTT state for momentary mode
static bool s_ptt_active = false;

// Band presets (Hz) for N1-N8 buttons
static const long BAND_FRESETS[] = {
    1840000,    // 160m
    3573000,    // 80m  (FT8)
    7074000,    // 40m  (FT8)
    14074000,   // 20m  (FT8)
    18100000,   // 17m
    21074000,   // 15m  (FT8)
    24915000,   // 12m
    28074000,   // 10m  (FT8)
};

// Mode cycle list
static const char *MODE_CYCLE[] = {
    "LSB", "USB", "CW", "AM", "FM", "DIGU", "DIGL"
};
#define MODE_CYCLE_LEN (sizeof(MODE_CYCLE) / sizeof(MODE_CYCLE[0]))
static int s_mode_cycle_index = 1;  // Start at USB

// ---------------------------------------------------------------------------
// Default mappings
// ---------------------------------------------------------------------------

static void add_default(const char *name, mapping_action_t action,
                        int32_t param_int, const char *param_str, uint8_t rx)
{
    if (s_mapping_count >= MAX_MAPPINGS) return;
    mapping_entry_t *e = &s_mappings[s_mapping_count++];
    strncpy(e->control_name, name, sizeof(e->control_name) - 1);
    e->action = action;
    e->param_int = param_int;
    if (param_str) {
        strncpy(e->param_str, param_str, sizeof(e->param_str) - 1);
    } else {
        e->param_str[0] = '\0';
    }
    e->rx = rx;
}

void mapping_engine_reset_defaults(void)
{
    s_mapping_count = 0;
    memset(s_mappings, 0, sizeof(s_mappings));

    // -- Deck A = RX/TRX 0 --

    // Jog wheel -> fine frequency tuning (10 Hz per tick)
    add_default("Jog_A",    MAP_ACTION_VFO_A_STEP, 10,    NULL, 0);
    // Pitch slider -> coarse frequency tuning (100 Hz per tick)
    add_default("Pitch_A",  MAP_ACTION_VFO_A_STEP, 100,   NULL, 0);

    // Volume slider -> audio volume
    add_default("Vol_A",    MAP_ACTION_VOLUME,      0,     NULL, 0);

    // EQ knobs -> filter
    add_default("Treble_A", MAP_ACTION_FILTER_WIDTH, 0,    NULL, 0);

    // Play -> PTT toggle
    add_default("Play_A",   MAP_ACTION_PTT_TOGGLE,  0,     NULL, 0);
    // CUE -> PTT momentary (TX while held)
    add_default("CUE_A",    MAP_ACTION_PTT_MOMENTARY, 0,   NULL, 0);

    // Listen -> mute toggle
    add_default("Listen_A", MAP_ACTION_MUTE_TOGGLE, 0,     NULL, 0);

    // Sync -> tune toggle
    add_default("Sync_A",   MAP_ACTION_TUNE_TOGGLE, 0,     NULL, 0);

    // N1-N8 -> band presets
    add_default("N1_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[0], NULL, 0);
    add_default("N2_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[1], NULL, 0);
    add_default("N3_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[2], NULL, 0);
    add_default("N4_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[3], NULL, 0);
    add_default("N5_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[4], NULL, 0);
    add_default("N6_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[5], NULL, 0);
    add_default("N7_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[6], NULL, 0);
    add_default("N8_A", MAP_ACTION_BAND_SELECT, BAND_FRESETS[7], NULL, 0);

    // Load -> mode cycle
    add_default("Load_A",   MAP_ACTION_MODE_CYCLE,  0,     NULL, 0);

    // Crossfader -> TX drive
    add_default("XFader",   MAP_ACTION_DRIVE,       0,     NULL, 0);

    // -- Deck B = RX/TRX 1 (second VFO) --
    add_default("Jog_B",    MAP_ACTION_VFO_B_STEP,  10,    NULL, 1);
    add_default("Pitch_B",  MAP_ACTION_VFO_B_STEP,  100,   NULL, 1);
    add_default("Vol_B",    MAP_ACTION_VOLUME_STEP,  1,    NULL, 1);
    add_default("Play_B",   MAP_ACTION_SPLIT_TOGGLE, 0,    NULL, 1);

    // FWD/RWD -> frequency step buttons (1kHz)
    add_default("FWD_A",    MAP_ACTION_VFO_A_STEP, 1000,   NULL, 0);
    add_default("RWD_A",    MAP_ACTION_VFO_A_STEP, -1000,  NULL, 0);
    add_default("FWD_B",    MAP_ACTION_VFO_B_STEP, 1000,   NULL, 1);
    add_default("RWD_B",    MAP_ACTION_VFO_B_STEP, -1000,  NULL, 1);

    ESP_LOGI(TAG, "Default mappings loaded (%d entries)", s_mapping_count);
}

// ---------------------------------------------------------------------------
// Mapping lookup
// ---------------------------------------------------------------------------

static mapping_entry_t *find_mapping(const char *control_name)
{
    for (int i = 0; i < s_mapping_count; i++) {
        if (strcmp(s_mappings[i].control_name, control_name) == 0) {
            return &s_mappings[i];
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Encoder delta calculation (handles wrap-around)
// ---------------------------------------------------------------------------

static int8_t encoder_delta(uint8_t old_val, uint8_t new_val)
{
    int diff = (int)new_val - (int)old_val;

    // Handle wrap-around: if diff > 127, it wrapped backwards
    if (diff > 127) diff -= 256;
    if (diff < -127) diff += 256;

    return (int8_t)diff;
}

// ---------------------------------------------------------------------------
// Command dispatch (TCI or CAT)
// ---------------------------------------------------------------------------

static void dispatch_set_vfo_a(long freq_hz)
{
    if (s_use_tci) {
        tci_client_set_vfo(0, 0, freq_hz);
    } else {
        cat_client_set_vfo_a(freq_hz);
    }
}

static void dispatch_set_vfo_b(long freq_hz)
{
    if (s_use_tci) {
        tci_client_set_vfo(0, 1, freq_hz);
    } else {
        cat_client_set_vfo_b(freq_hz);
    }
}

static void dispatch_set_mode(const char *mode)
{
    if (s_use_tci) {
        tci_client_set_mode(0, mode);
    } else {
        // Map mode string to CAT mode code
        cat_mode_t cat_mode = CAT_MODE_USB;
        if (strcmp(mode, "LSB") == 0) cat_mode = CAT_MODE_LSB;
        else if (strcmp(mode, "USB") == 0) cat_mode = CAT_MODE_USB;
        else if (strcmp(mode, "CW") == 0) cat_mode = CAT_MODE_CW;
        else if (strcmp(mode, "AM") == 0) cat_mode = CAT_MODE_AM;
        else if (strcmp(mode, "FM") == 0) cat_mode = CAT_MODE_FM;
        else if (strcmp(mode, "DIGL") == 0) cat_mode = CAT_MODE_DIGL;
        else if (strcmp(mode, "DIGU") == 0) cat_mode = CAT_MODE_DIGU;
        cat_client_set_mode(cat_mode);
    }
}

static void dispatch_set_volume(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    if (s_use_tci) {
        // TCI volume is not directly mapped in parseTextFrame -
        // use mon_volume or rx_volume. For now use a custom command.
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "volume:%d", level);
        tci_client_send(cmd);
    } else {
        cat_client_set_volume(level);
    }
}

static void dispatch_set_drive(int power)
{
    if (power < 0) power = 0;
    if (power > 100) power = 100;
    if (s_use_tci) {
        tci_client_set_drive(0, power);
    } else {
        cat_client_set_drive(power);
    }
}

static void dispatch_set_ptt(bool tx)
{
    if (s_use_tci) {
        tci_client_set_ptt(0, tx);
    } else {
        cat_client_set_ptt(tx);
    }
}

static void dispatch_set_tune(bool tune)
{
    if (s_use_tci) {
        tci_client_set_tune(0, tune);
    } else {
        cat_client_set_tune(tune);
    }
}

static void dispatch_set_mute(bool mute)
{
    if (s_use_tci) {
        tci_client_set_mute(mute);
    } else {
        cat_client_set_mute(mute);
    }
}

static void dispatch_set_split(bool split)
{
    if (s_use_tci) {
        tci_client_set_split(0, split);
    } else {
        cat_client_set_split(split);
    }
}

// ---------------------------------------------------------------------------
// Action execution
// ---------------------------------------------------------------------------

// Toggle state trackers
static bool s_mute_state = false;
static bool s_ptt_state = false;
static bool s_tune_state = false;
static bool s_split_state = false;

static void execute_action(const mapping_entry_t *m, dj_control_type_t ctrl_type,
                           uint8_t old_val, uint8_t new_val)
{
    switch (m->action) {

    case MAP_ACTION_VFO_A_STEP: {
        // Encoder: delta * step_size. Button: single step on press.
        long step = m->param_int;
        if (ctrl_type == DJ_CTRL_ENCODER) {
            int8_t delta = encoder_delta(old_val, new_val);
            step *= delta;
        } else if (ctrl_type == DJ_CTRL_BUTTON) {
            if (new_val == 0) return;  // Only on press
        }
        const tci_radio_state_t *rs = tci_client_get_radio_state();
        long new_freq = rs->vfo_a_freq + step;
        if (new_freq > 0) {
            dispatch_set_vfo_a(new_freq);
        }
        break;
    }

    case MAP_ACTION_VFO_B_STEP: {
        long step = m->param_int;
        if (ctrl_type == DJ_CTRL_ENCODER) {
            int8_t delta = encoder_delta(old_val, new_val);
            step *= delta;
        } else if (ctrl_type == DJ_CTRL_BUTTON) {
            if (new_val == 0) return;
        }
        const tci_radio_state_t *rs = tci_client_get_radio_state();
        long new_freq = rs->vfo_b_freq + step;
        if (new_freq > 0) {
            dispatch_set_vfo_b(new_freq);
        }
        break;
    }

    case MAP_ACTION_BAND_SELECT:
        if (new_val == 0) return;  // Only on press
        ESP_LOGI(TAG, "Band select: %ld Hz", (long)m->param_int);
        dispatch_set_vfo_a(m->param_int);
        break;

    case MAP_ACTION_MODE_SET:
        if (new_val == 0) return;
        dispatch_set_mode(m->param_str);
        break;

    case MAP_ACTION_MODE_CYCLE:
        if (new_val == 0) return;
        s_mode_cycle_index = (s_mode_cycle_index + 1) % MODE_CYCLE_LEN;
        ESP_LOGI(TAG, "Mode cycle: %s", MODE_CYCLE[s_mode_cycle_index]);
        dispatch_set_mode(MODE_CYCLE[s_mode_cycle_index]);
        break;

    case MAP_ACTION_VOLUME:
        // Dial: 0x00-0xFF -> 0-100
        dispatch_set_volume((new_val * 100) / 255);
        break;

    case MAP_ACTION_VOLUME_STEP: {
        if (ctrl_type == DJ_CTRL_ENCODER) {
            // TODO: track current volume and step
        }
        break;
    }

    case MAP_ACTION_PTT_TOGGLE:
        if (new_val == 0) return;  // Only on press
        s_ptt_state = !s_ptt_state;
        ESP_LOGI(TAG, "PTT toggle: %s", s_ptt_state ? "TX" : "RX");
        dispatch_set_ptt(s_ptt_state);
        break;

    case MAP_ACTION_PTT_MOMENTARY:
        // TX on press, RX on release
        if (new_val > 0 && !s_ptt_active) {
            s_ptt_active = true;
            ESP_LOGI(TAG, "PTT momentary: TX");
            dispatch_set_ptt(true);
        } else if (new_val == 0 && s_ptt_active) {
            s_ptt_active = false;
            ESP_LOGI(TAG, "PTT momentary: RX");
            dispatch_set_ptt(false);
        }
        break;

    case MAP_ACTION_TUNE_TOGGLE:
        if (new_val == 0) return;
        s_tune_state = !s_tune_state;
        ESP_LOGI(TAG, "Tune: %s", s_tune_state ? "ON" : "OFF");
        dispatch_set_tune(s_tune_state);
        break;

    case MAP_ACTION_DRIVE:
        // Dial: 0x00-0xFF -> 0-100
        dispatch_set_drive((new_val * 100) / 255);
        break;

    case MAP_ACTION_DRIVE_STEP: {
        if (ctrl_type == DJ_CTRL_ENCODER) {
            // TODO: track current drive and step
        }
        break;
    }

    case MAP_ACTION_FILTER_WIDTH: {
        // Dial: map to filter bandwidth.
        // 0x00 = narrowest (200Hz), 0xFF = widest (6000Hz)
        int bw = 200 + (new_val * (6000 - 200)) / 255;
        int center = bw / 2;
        if (s_use_tci) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "rx_filter_band:0,%d,%d", -center, center);
            tci_client_send(cmd);
        }
        break;
    }

    case MAP_ACTION_FILTER_SHIFT: {
        // TODO: encoder-based filter shift
        break;
    }

    case MAP_ACTION_MUTE_TOGGLE:
        if (new_val == 0) return;
        s_mute_state = !s_mute_state;
        dispatch_set_mute(s_mute_state);
        break;

    case MAP_ACTION_SPLIT_TOGGLE:
        if (new_val == 0) return;
        s_split_state = !s_split_state;
        dispatch_set_split(s_split_state);
        break;

    case MAP_ACTION_CUSTOM_TCI:
        if (new_val == 0 && ctrl_type == DJ_CTRL_BUTTON) return;
        tci_client_send(m->param_str);
        break;

    case MAP_ACTION_CUSTOM_CAT:
        if (new_val == 0 && ctrl_type == DJ_CTRL_BUTTON) return;
        cat_client_send(m->param_str);
        break;

    case MAP_ACTION_NONE:
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// JSON serialization / deserialization
// ---------------------------------------------------------------------------

static const char *action_to_str(mapping_action_t action)
{
    switch (action) {
    case MAP_ACTION_NONE:           return "none";
    case MAP_ACTION_VFO_A_STEP:     return "vfo_a_step";
    case MAP_ACTION_VFO_B_STEP:     return "vfo_b_step";
    case MAP_ACTION_VFO_A_DIRECT:   return "vfo_a_direct";
    case MAP_ACTION_BAND_SELECT:    return "band_select";
    case MAP_ACTION_MODE_SET:       return "mode_set";
    case MAP_ACTION_MODE_CYCLE:     return "mode_cycle";
    case MAP_ACTION_VOLUME:         return "volume";
    case MAP_ACTION_VOLUME_STEP:    return "volume_step";
    case MAP_ACTION_PTT_TOGGLE:     return "ptt_toggle";
    case MAP_ACTION_PTT_MOMENTARY:  return "ptt_momentary";
    case MAP_ACTION_TUNE_TOGGLE:    return "tune_toggle";
    case MAP_ACTION_DRIVE:          return "drive";
    case MAP_ACTION_DRIVE_STEP:     return "drive_step";
    case MAP_ACTION_FILTER_WIDTH:   return "filter_width";
    case MAP_ACTION_FILTER_SHIFT:   return "filter_shift";
    case MAP_ACTION_MUTE_TOGGLE:    return "mute_toggle";
    case MAP_ACTION_SPLIT_TOGGLE:   return "split_toggle";
    case MAP_ACTION_CUSTOM_TCI:     return "custom_tci";
    case MAP_ACTION_CUSTOM_CAT:     return "custom_cat";
    default:                        return "none";
    }
}

static mapping_action_t str_to_action(const char *str)
{
    if (!str) return MAP_ACTION_NONE;
    if (strcmp(str, "vfo_a_step") == 0) return MAP_ACTION_VFO_A_STEP;
    if (strcmp(str, "vfo_b_step") == 0) return MAP_ACTION_VFO_B_STEP;
    if (strcmp(str, "vfo_a_direct") == 0) return MAP_ACTION_VFO_A_DIRECT;
    if (strcmp(str, "band_select") == 0) return MAP_ACTION_BAND_SELECT;
    if (strcmp(str, "mode_set") == 0) return MAP_ACTION_MODE_SET;
    if (strcmp(str, "mode_cycle") == 0) return MAP_ACTION_MODE_CYCLE;
    if (strcmp(str, "volume") == 0) return MAP_ACTION_VOLUME;
    if (strcmp(str, "volume_step") == 0) return MAP_ACTION_VOLUME_STEP;
    if (strcmp(str, "ptt_toggle") == 0) return MAP_ACTION_PTT_TOGGLE;
    if (strcmp(str, "ptt_momentary") == 0) return MAP_ACTION_PTT_MOMENTARY;
    if (strcmp(str, "tune_toggle") == 0) return MAP_ACTION_TUNE_TOGGLE;
    if (strcmp(str, "drive") == 0) return MAP_ACTION_DRIVE;
    if (strcmp(str, "drive_step") == 0) return MAP_ACTION_DRIVE_STEP;
    if (strcmp(str, "filter_width") == 0) return MAP_ACTION_FILTER_WIDTH;
    if (strcmp(str, "filter_shift") == 0) return MAP_ACTION_FILTER_SHIFT;
    if (strcmp(str, "mute_toggle") == 0) return MAP_ACTION_MUTE_TOGGLE;
    if (strcmp(str, "split_toggle") == 0) return MAP_ACTION_SPLIT_TOGGLE;
    if (strcmp(str, "custom_tci") == 0) return MAP_ACTION_CUSTOM_TCI;
    if (strcmp(str, "custom_cat") == 0) return MAP_ACTION_CUSTOM_CAT;
    return MAP_ACTION_NONE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t mapping_engine_init(void)
{
    // Try to load saved mappings
    if (mapping_engine_load() != ESP_OK) {
        // No saved mappings - use defaults
        mapping_engine_reset_defaults();
    }

    // Check protocol preference
    char proto[8] = {0};
    if (config_get_str(CFG_KEY_PROTOCOL, proto, sizeof(proto)) == ESP_OK) {
        s_use_tci = (strcmp(proto, "cat") != 0);
    }

    ESP_LOGI(TAG, "Mapping engine initialized (%d mappings, protocol=%s)",
             s_mapping_count, s_use_tci ? "TCI" : "CAT");
    return ESP_OK;
}

void mapping_engine_on_control(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value)
{
    mapping_entry_t *m = find_mapping(name);
    if (!m || m->action == MAP_ACTION_NONE) {
        return;  // Unmapped control
    }

    execute_action(m, control_type, old_value, new_value);
}

const mapping_entry_t *mapping_engine_get_table(int *count)
{
    if (count) *count = s_mapping_count;
    return s_mappings;
}

esp_err_t mapping_engine_set(const mapping_entry_t *entry)
{
    // Update existing entry or add new
    mapping_entry_t *existing = find_mapping(entry->control_name);
    if (existing) {
        memcpy(existing, entry, sizeof(mapping_entry_t));
        return ESP_OK;
    }

    if (s_mapping_count >= MAX_MAPPINGS) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(&s_mappings[s_mapping_count++], entry, sizeof(mapping_entry_t));
    return ESP_OK;
}

esp_err_t mapping_engine_remove(const char *control_name)
{
    for (int i = 0; i < s_mapping_count; i++) {
        if (strcmp(s_mappings[i].control_name, control_name) == 0) {
            // Shift remaining entries down
            for (int j = i; j < s_mapping_count - 1; j++) {
                s_mappings[j] = s_mappings[j + 1];
            }
            s_mapping_count--;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t mapping_engine_save(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return ESP_ERR_NO_MEM;

    for (int i = 0; i < s_mapping_count; i++) {
        const mapping_entry_t *e = &s_mappings[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "n", e->control_name);
        cJSON_AddStringToObject(obj, "a", action_to_str(e->action));
        cJSON_AddNumberToObject(obj, "p", e->param_int);
        if (e->param_str[0]) {
            cJSON_AddStringToObject(obj, "s", e->param_str);
        }
        cJSON_AddNumberToObject(obj, "r", e->rx);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (!json) return ESP_ERR_NO_MEM;

    esp_err_t err = config_set_blob(CFG_KEY_MAPPINGS, json, strlen(json));
    free(json);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Mappings saved to NVS (%d entries)", s_mapping_count);
    }
    return err;
}

esp_err_t mapping_engine_load(void)
{
    size_t len = 0;
    char *json = config_get_blob(CFG_KEY_MAPPINGS, &len);
    if (!json) return ESP_ERR_NOT_FOUND;

    cJSON *arr = cJSON_Parse(json);
    free(json);

    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return ESP_ERR_INVALID_ARG;
    }

    s_mapping_count = 0;
    memset(s_mappings, 0, sizeof(s_mappings));

    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (s_mapping_count >= MAX_MAPPINGS) break;

        mapping_entry_t *e = &s_mappings[s_mapping_count];

        cJSON *n = cJSON_GetObjectItem(item, "n");
        cJSON *a = cJSON_GetObjectItem(item, "a");
        cJSON *p = cJSON_GetObjectItem(item, "p");
        cJSON *s = cJSON_GetObjectItem(item, "s");
        cJSON *r = cJSON_GetObjectItem(item, "r");

        if (n && cJSON_IsString(n)) {
            strncpy(e->control_name, n->valuestring, sizeof(e->control_name) - 1);
        }
        if (a && cJSON_IsString(a)) {
            e->action = str_to_action(a->valuestring);
        }
        if (p && cJSON_IsNumber(p)) {
            e->param_int = (int32_t)p->valuedouble;
        }
        if (s && cJSON_IsString(s)) {
            strncpy(e->param_str, s->valuestring, sizeof(e->param_str) - 1);
        }
        if (r && cJSON_IsNumber(r)) {
            e->rx = (uint8_t)r->valueint;
        }

        s_mapping_count++;
    }

    cJSON_Delete(arr);
    ESP_LOGI(TAG, "Loaded %d mappings from NVS", s_mapping_count);
    return ESP_OK;
}

void mapping_engine_set_protocol(bool use_tci)
{
    s_use_tci = use_tci;
    config_set_str(CFG_KEY_PROTOCOL, use_tci ? "tci" : "cat");
    ESP_LOGI(TAG, "Protocol set to %s", use_tci ? "TCI" : "CAT");
}

bool mapping_engine_get_protocol(void)
{
    return s_use_tci;
}
