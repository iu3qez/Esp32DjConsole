#include "mapping_engine.h"
#include "cat_client.h"
#include "dj_led.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "mapper";

// ===================================================================
// Thetis Command Database — auto-generated from reference/CATCommands.cs
// ===================================================================

#include "cmd_db_generated.inc"

#define CMD_DB_COUNT (sizeof(s_cmd_db) / sizeof(s_cmd_db[0]))

static const char *s_category_names[] = {
    [CAT_VFO]       = "VFO",
    [CAT_BAND]      = "Band",
    [CAT_MODE]      = "Mode",
    [CAT_TX]        = "TX",
    [CAT_AUDIO]     = "Audio",
    [CAT_FILTER]    = "Filter",
    [CAT_NR_NB]     = "NR/NB",
    [CAT_AGC]       = "AGC",
    [CAT_SPLIT_RIT] = "Split/RIT/XIT",
    [CAT_CW]        = "CW",
    [CAT_MISC]      = "Misc",
};

const thetis_cmd_t *cmd_db_get_all(int *count)
{
    if (count) *count = CMD_DB_COUNT;
    return s_cmd_db;
}

const thetis_cmd_t *cmd_db_find(uint16_t id)
{
    for (int i = 0; i < (int)CMD_DB_COUNT; i++) {
        if (s_cmd_db[i].id == id) return &s_cmd_db[i];
    }
    return NULL;
}

const char *cmd_category_name(cmd_category_t cat)
{
    if (cat >= 0 && cat < CAT_CATEGORY_COUNT) return s_category_names[cat];
    return "Unknown";
}

// ===================================================================
// Mapping state
// ===================================================================

static mapping_entry_t s_mappings[MAX_MAPPINGS];
static int s_mapping_count = 0;

// Toggle state tracker (tracks command ID, state, and CAT command for sync)
#define TOGGLE_SLOTS 32
static struct {
    uint16_t id;
    bool state;
    char cat_cmd[5];  // "ZZRT", "ZZMA", etc.
} s_toggles[TOGGLE_SLOTS];
static int s_toggle_count = 0;

// VFO freq tracking for FREQ exec type (synced from Thetis via CAT responses)
typedef struct {
    long freq;
    bool synced;
} vfo_state_t;

static vfo_state_t s_vfo_a = { .freq = 0, .synced = false };
static vfo_state_t s_vfo_b = { .freq = 0, .synced = false };

// Filter edge tracking for FILTER_WIDTH exec type (synced from Thetis ZZFH/ZZFL)
typedef struct {
    int hi;         // Upper edge (Hz), from ZZFH
    int lo;         // Lower edge (Hz), from ZZFL
    int width;      // Tracked width for encoder-relative mode (0 = use current edges)
    bool synced;    // Must sync from Thetis before first use
} filter_state_t;

static filter_state_t s_filter = { .hi = 1000, .lo = 25, .width = 0, .synced = false };

// Tuning step from Thetis ZZAC response (index 0-25 -> Hz)
static const int s_step_table[] = {
    1, 2, 10, 25, 50, 100, 250, 500,           // 0-7
    1000, 2000, 2500, 5000, 6250, 9000,         // 8-13
    10000, 12500, 15000, 20000, 25000, 30000,   // 14-19
    50000, 100000, 250000, 500000, 1000000, 10000000  // 20-25
};
#define STEP_TABLE_SIZE (sizeof(s_step_table) / sizeof(s_step_table[0]))
static int s_tune_step_hz = 10;  // default 10 Hz, updated from ZZAC if supported

// SET value tracker — for encoder-as-relative on CMD_CAT_SET commands
#define SET_SLOTS 16
static struct {
    uint16_t cmd_id;
    int32_t  value;
    bool     inited;
} s_set_state[SET_SLOTS];
static int s_set_count = 0;

static int32_t *find_set_value(uint16_t cmd_id, int value_min, int value_max)
{
    for (int i = 0; i < s_set_count; i++) {
        if (s_set_state[i].cmd_id == cmd_id) return &s_set_state[i].value;
    }
    if (s_set_count < SET_SLOTS) {
        s_set_state[s_set_count].cmd_id = cmd_id;
        s_set_state[s_set_count].value = (value_min + value_max) / 2;  // start at midpoint
        s_set_state[s_set_count].inited = true;
        return &s_set_state[s_set_count++].value;
    }
    return NULL;
}

// Velocity scaling: time between encoder ticks determines step multiplier
#define VELOCITY_MAX_MULTIPLIER 10
#define VELOCITY_SLOW_US  200000  // >= 200ms between ticks = 1x (slow turning)
#define VELOCITY_FAST_US   50000  // <= 50ms between ticks = 10x (fast turning)
static int64_t s_last_freq_tick_us = 0;  // timestamp of last encoder tick

#define MAPPINGS_PATH "/www/mappings.json"

// ===================================================================
// Learn mode state
// ===================================================================

static bool s_learn_active = false;
static uint16_t s_learn_command_id = 0;
static int64_t s_learn_start_us = 0;
#define LEARN_TIMEOUT_US (15 * 1000000LL)  // 15 seconds

static mapping_learn_callback_t s_learn_cb = NULL;
static mapping_cat_callback_t s_cat_cb = NULL;

// ===================================================================
// Helpers
// ===================================================================

static mapping_entry_t *find_mapping(const char *control_name)
{
    for (int i = 0; i < s_mapping_count; i++) {
        if (strcmp(s_mappings[i].control_name, control_name) == 0)
            return &s_mappings[i];
    }
    return NULL;
}

static int8_t encoder_delta(uint8_t old_val, uint8_t new_val)
{
    int diff = (int)new_val - (int)old_val;
    if (diff > 127) diff -= 256;
    if (diff < -127) diff += 256;
    return (int8_t)diff;
}

// Snap frequency to step boundary (from Thetis SnapTune logic)
static long snap_tune(long freq, int step, int direction)
{
    if (step == 0) return freq;
    long snapped = (freq / step) * step;
    int orig_dir = direction;
    // If already on boundary and going down, the integer division handled one step
    if (direction < 0 && freq % step != 0)
        direction++;  // off boundary, division already stepped down
    snapped += (long)direction * step;
    ESP_LOGD(TAG, "VFO_DBG snap_tune: in=%ld step=%d dir=%d->%d mod=%ld snapped=%ld",
             freq, step, orig_dir, direction, freq % step, snapped);
    return snapped;
}

// Velocity multiplier based on time between encoder ticks.
// Fast turning (short interval) = high multiplier, slow = 1x.
static int velocity_multiplier(void)
{
    int64_t now = esp_timer_get_time();
    int64_t interval = now - s_last_freq_tick_us;
    s_last_freq_tick_us = now;

    if (interval <= 0 || interval >= VELOCITY_SLOW_US) return 1;
    if (interval <= VELOCITY_FAST_US) return VELOCITY_MAX_MULTIPLIER;

    // Linear interpolation: fast..slow -> max..1
    int range_us = VELOCITY_SLOW_US - VELOCITY_FAST_US;
    int from_fast = (int)(interval - VELOCITY_FAST_US);
    return VELOCITY_MAX_MULTIPLIER - (from_fast * (VELOCITY_MAX_MULTIPLIER - 1)) / range_us;
}

static bool *find_toggle(uint16_t cmd_id, const char *cat_cmd)
{
    for (int i = 0; i < s_toggle_count; i++) {
        if (s_toggles[i].id == cmd_id) return &s_toggles[i].state;
    }
    if (s_toggle_count < TOGGLE_SLOTS) {
        s_toggles[s_toggle_count].id = cmd_id;
        s_toggles[s_toggle_count].state = false;
        if (cat_cmd) {
            strncpy(s_toggles[s_toggle_count].cat_cmd, cat_cmd, sizeof(s_toggles[0].cat_cmd) - 1);
            s_toggles[s_toggle_count].cat_cmd[sizeof(s_toggles[0].cat_cmd) - 1] = '\0';
        } else {
            s_toggles[s_toggle_count].cat_cmd[0] = '\0';
        }
        return &s_toggles[s_toggle_count++].state;
    }
    return NULL;
}

// ===================================================================
// Control name -> LED note lookup
// ===================================================================

static const struct { const char *control; uint8_t note; } s_led_map[] = {
    // Deck A
    {"Play_A",     LED_PLAY_A},
    {"CUE_A",      LED_CUE_A},
    {"Sync_A",     LED_SYNC_A},
    {"Listen_A",   LED_LISTEN_A},
    {"N1_A",       LED_N1_A},
    {"N2_A",       LED_N2_A},
    {"N3_A",       LED_N3_A},
    {"N4_A",       LED_N4_A},
    {"N5_A",       LED_N5_A},
    {"N6_A",       LED_N6_A},
    {"N7_A",       LED_N7_A},
    {"N8_A",       LED_N8_A},
    // Deck B
    {"Play_B",     LED_PLAY_B},
    {"CUE_B",      LED_CUE_B},
    {"Sync_B",     LED_SYNC_B},
    {"Listen_B",   LED_LISTEN_B},
    {"N1_B",       LED_N1_B},
    {"N2_B",       LED_N2_B},
    {"N3_B",       LED_N3_B},
    {"N4_B",       LED_N4_B},
    {"N5_B",       LED_N5_B},
    {"N6_B",       LED_N6_B},
    {"N7_B",       LED_N7_B},
    {"N8_B",       LED_N8_B},
    // Global
    {"Up",         LED_UP},
    {"Down",       LED_DOWN},
};
#define LED_MAP_COUNT (sizeof(s_led_map) / sizeof(s_led_map[0]))

static uint8_t find_led_note(const char *control_name)
{
    for (int i = 0; i < (int)LED_MAP_COUNT; i++) {
        if (strcmp(s_led_map[i].control, control_name) == 0)
            return s_led_map[i].note;
    }
    return 0;  // 0 = no LED for this control
}

// ===================================================================
// Toggle LED sync helper
// ===================================================================

static void update_toggle_led(uint16_t cmd_id, bool state)
{
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].command_id == cmd_id) {
            uint8_t note = find_led_note(s_mappings[i].control_name);
            if (note > 0) {
                dj_led_set(note, state);
            }
        }
    }
}

// ===================================================================
// CAT command execution
// ===================================================================

static void notify_cat(const char *control_name, const thetis_cmd_t *cmd, const char *cat_str)
{
    if (s_cat_cb) s_cat_cb(control_name, cmd->name, cmd->exec_type, cat_str);
}

static void exec_button(const thetis_cmd_t *cmd, const char *control_name,
                        dj_control_type_t ctrl_type, uint8_t new_val)
{
    char buf[32];
    // Only fire on button press (not release), or on any encoder/dial change
    if (ctrl_type == DJ_CTRL_BUTTON && new_val == 0) return;
    if (cmd->value_digits > 0) {
        snprintf(buf, sizeof(buf), "%s%0*d;", cmd->cat_cmd,
                 cmd->value_digits, cmd->value_min);
    } else {
        snprintf(buf, sizeof(buf), "%s;", cmd->cat_cmd);
    }
    cat_client_send(buf);
    notify_cat(control_name, cmd, buf);
    ESP_LOGI(TAG, "CMD [%s] -> %s", cmd->name, buf);
    // Re-query step after ZZSU/ZZSD so local step stays in sync
    if (strcmp(cmd->cat_cmd, "ZZSU") == 0 || strcmp(cmd->cat_cmd, "ZZSD") == 0) {
        cat_client_send("ZZAC;");
    }
}

static void exec_toggle(const thetis_cmd_t *cmd, const char *control_name,
                         dj_control_type_t ctrl_type, uint8_t new_val)
{
    char buf[32];
    if (ctrl_type == DJ_CTRL_BUTTON && new_val == 0) return;
    bool *state = find_toggle(cmd->id, cmd->cat_cmd);
    if (!state) return;
    *state = !(*state);
    snprintf(buf, sizeof(buf), "%s%0*d;", cmd->cat_cmd,
             cmd->value_digits, *state ? cmd->value_max : cmd->value_min);
    cat_client_send(buf);
    notify_cat(control_name, cmd, buf);
    ESP_LOGI(TAG, "TOGGLE [%s] -> %s (state=%d)", cmd->name, buf, *state);
}

static void exec_set(const thetis_cmd_t *cmd, const char *control_name,
                     dj_control_type_t ctrl_type, uint8_t old_val, uint8_t new_val, int32_t param)
{
    char buf[32];
    int val;
    if (ctrl_type == DJ_CTRL_ENCODER) {
        // Encoder: relative inc/dec, step = param (default 1)
        int8_t delta = encoder_delta(old_val, new_val);
        if (delta == 0) return;
        int step = (param > 0) ? param : 1;
        int32_t *tracked = find_set_value(cmd->id, cmd->value_min, cmd->value_max);
        if (!tracked) return;
        *tracked += delta * step;
        if (*tracked > cmd->value_max) *tracked = cmd->value_max;
        if (*tracked < cmd->value_min) *tracked = cmd->value_min;
        val = *tracked;
    } else {
        // Knob/slider: scale 0-255 to value_min..value_max
        int range = cmd->value_max - cmd->value_min;
        val = cmd->value_min + (new_val * range) / 255;
    }
    snprintf(buf, sizeof(buf), "%s%0*d;", cmd->cat_cmd,
             cmd->value_digits, val);
    cat_client_send(buf);
    notify_cat(control_name, cmd, buf);
    ESP_LOGD(TAG, "SET [%s] raw=%d -> val=%d -> %s", cmd->name, new_val, val, buf);
}

static void exec_freq(const thetis_cmd_t *cmd, const char *control_name,
                      dj_control_type_t ctrl_type, uint8_t old_val, uint8_t new_val, int32_t param)
{
    char buf[32];
    // Read-modify-write VFO frequency (same logic as midi2cat ChangeFreqVfoA/B)
    // Step from Thetis ZZAC, velocity-scaled by wheel speed
    int8_t delta = 0;
    if (ctrl_type == DJ_CTRL_ENCODER) {
        delta = encoder_delta(old_val, new_val);
    } else if (ctrl_type == DJ_CTRL_BUTTON) {
        if (new_val == 0) return;
        delta = (param > 0) ? 1 : -1;  // Button direction from param sign
    } else {
        delta = (new_val > old_val) ? 1 : -1;
    }
    if (delta == 0) return;

    vfo_state_t *vfo = NULL;
    const char *vfo_label = "?";
    if (strcmp(cmd->cat_cmd, "ZZFA") == 0) {
        vfo = &s_vfo_a;
        vfo_label = "A";
    } else if (strcmp(cmd->cat_cmd, "ZZFB") == 0) {
        vfo = &s_vfo_b;
        vfo_label = "B";
    }
    if (!vfo) return;

    // Check idle gap BEFORE velocity_multiplier() updates s_last_freq_tick_us
    int64_t now_us = esp_timer_get_time();
    int64_t gap = now_us - s_last_freq_tick_us;

    ESP_LOGI(TAG, "VFO_DBG [%s] VFO_%s tick: delta=%d old=%d new=%d synced=%d "
             "cached_freq=%ld gap=%lld ms",
             cmd->name, vfo_label, delta, old_val, new_val,
             vfo->synced, vfo->freq, gap / 1000);

    if (!vfo->synced) {
        ESP_LOGW(TAG, "VFO_DBG [%s] SKIPPED — VFO_%s not synced yet", cmd->name, vfo_label);
        return;
    }

    // Re-sync from Thetis on first tick after idle (>1s gap).
    // Catches band/freq changes made in Thetis UI. Skip this tick;
    // response arrives before next tick so subsequent ticks use fresh data.
    if (gap > 1000000) {  // >1 second since last FREQ tick
        vfo->synced = false;
        const char *query = (strcmp(cmd->cat_cmd, "ZZFA") == 0) ? "ZZFA;" : "ZZFB;";
        cat_client_send(query);
        ESP_LOGI(TAG, "VFO_DBG [%s] RE-SYNC: gap=%lld ms > 1s, sent %s "
                 "invalidated VFO_%s (was %ld Hz)",
                 cmd->name, gap / 1000, query, vfo_label, vfo->freq);
        // Update timestamp so velocity_multiplier starts fresh after resync
        s_last_freq_tick_us = now_us;
        return;
    }

    // Step: use Thetis step (from ZZAC), scaled by tick-rate velocity
    int base_step = s_tune_step_hz;
    int mult = velocity_multiplier();
    int step_hz = base_step * mult;

    long old_freq = vfo->freq;
    int direction = (delta > 0) ? 1 : -1;
    vfo->freq = snap_tune(vfo->freq, step_hz, direction);
    if (vfo->freq < 100000) vfo->freq = 100000;
    if (vfo->freq > 54000000) vfo->freq = 54000000;
    snprintf(buf, sizeof(buf), "%s%011ld;", cmd->cat_cmd, vfo->freq);
    cat_client_send(buf);
    notify_cat(control_name, cmd, buf);
    ESP_LOGI(TAG, "VFO_DBG [%s] TUNE: %ld -> %ld Hz (dir=%d step=%d x%d=%d) cmd='%s'",
             cmd->name, old_freq, vfo->freq, direction, base_step, mult, step_hz, buf);
}

static void exec_wheel(const thetis_cmd_t *cmd, const char *control_name,
                       dj_control_type_t ctrl_type, uint8_t old_val, uint8_t new_val)
{
    char buf[32];
    int8_t delta = 0;
    if (ctrl_type == DJ_CTRL_ENCODER) {
        delta = encoder_delta(old_val, new_val);
    } else if (ctrl_type == DJ_CTRL_BUTTON) {
        if (new_val == 0) return;
        delta = 1;
    }
    // Send inc command for positive delta, dec for negative
    const char *c = (delta > 0) ? cmd->cat_cmd : cmd->cat_cmd2;
    if (!c) return;
    int count = abs(delta);
    if (count > 10) count = 10;  // Sanity limit
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "%s;", c);
        cat_client_send(buf);
    }
    notify_cat(control_name, cmd, buf);
    ESP_LOGD(TAG, "WHEEL [%s] delta=%d x%d", cmd->name, delta, count);
}

static void exec_filter_width(const thetis_cmd_t *cmd, const char *control_name,
                              dj_control_type_t ctrl_type, uint8_t old_val, uint8_t new_val, int32_t param)
{
    char buf[32];
    ESP_LOGI(TAG, "FW_DBG [%s] tick: ctrl_type=%d old=%d new=%d, synced=%d, lo=%d hi=%d width=%d",
             control_name, ctrl_type, old_val, new_val, s_filter.synced,
             s_filter.lo, s_filter.hi, s_filter.width);
    if (!s_filter.synced) {
        cat_client_send("ZZFH;");
        cat_client_send("ZZFL;");
        ESP_LOGW(TAG, "FW_DBG [%s] NOT SYNCED — querying ZZFH/ZZFL, skipping tick", cmd->name);
        return;
    }
    int wmin = cmd->value_min > 0 ? cmd->value_min : 50;
    int wmax = cmd->value_max > 0 ? cmd->value_max : 6000;
    int step = (param > 0) ? param : 50;  // default 50 Hz per encoder tick
    int width;

    if (ctrl_type == DJ_CTRL_ENCODER) {
        // Encoder: relative inc/dec of tracked width
        int8_t delta = encoder_delta(old_val, new_val);
        if (delta == 0) return;
        // Initialize tracked width from current filter edges on first use
        if (s_filter.width == 0) {
            s_filter.width = s_filter.hi - s_filter.lo;
            if (s_filter.width < wmin) s_filter.width = wmin;
        }
        s_filter.width += delta * step;
        if (s_filter.width < wmin) s_filter.width = wmin;
        if (s_filter.width > wmax) s_filter.width = wmax;
        width = s_filter.width;
    } else {
        // Knob/slider: scale 0-255 to width range
        width = wmin + (new_val * (wmax - wmin)) / 255;
        s_filter.width = width;
    }

    int center = (s_filter.hi + s_filter.lo) / 2;
    ESP_LOGI(TAG, "FW_DBG [%s] calc: wmin=%d wmax=%d step=%d raw_center=%d width=%d",
             cmd->name, wmin, wmax, step, center, width);
    if (center < 0) center = -center;  // abs for LSB
    // ZZSF takes center (4 digits) + width (4 digits) per Thetis CATCommands.cs
    snprintf(buf, sizeof(buf), "ZZSF%04d%04d;", center, width);
    ESP_LOGI(TAG, "FW_DBG [%s] result: center=%d width=%d CAT_CMD='%s'",
             cmd->name, center, width, buf);
    cat_client_send(buf);
    // Update local tracking to reflect what Thetis will set
    s_filter.lo = center - width / 2;
    s_filter.hi = center + width / 2;
    if (s_filter.lo < 0) s_filter.lo = 0;
    notify_cat(control_name, cmd, buf);
}

static void execute_command(const thetis_cmd_t *cmd, const char *control_name,
                            dj_control_type_t ctrl_type,
                            uint8_t old_val, uint8_t new_val, int32_t param)
{
    switch (cmd->exec_type) {
    case CMD_CAT_BUTTON:       exec_button(cmd, control_name, ctrl_type, new_val); break;
    case CMD_CAT_TOGGLE:       exec_toggle(cmd, control_name, ctrl_type, new_val); break;
    case CMD_CAT_SET:          exec_set(cmd, control_name, ctrl_type, old_val, new_val, param); break;
    case CMD_CAT_FREQ:         exec_freq(cmd, control_name, ctrl_type, old_val, new_val, param); break;
    case CMD_CAT_WHEEL:        exec_wheel(cmd, control_name, ctrl_type, old_val, new_val); break;
    case CMD_CAT_FILTER_WIDTH: exec_filter_width(cmd, control_name, ctrl_type, old_val, new_val, param); break;
    }
}

// ===================================================================
// Default mappings
// ===================================================================

static void add_default(const char *name, uint16_t cmd_id, int32_t param)
{
    if (s_mapping_count >= MAX_MAPPINGS) return;
    mapping_entry_t *e = &s_mappings[s_mapping_count++];
    strncpy(e->control_name, name, sizeof(e->control_name) - 1);
    e->control_name[sizeof(e->control_name) - 1] = '\0';
    e->command_id = cmd_id;
    e->param = param;
}

void mapping_engine_reset_defaults(void)
{
    s_mapping_count = 0;
    memset(s_mappings, 0, sizeof(s_mappings));

    // -- Deck A --  (CMD_CAT_FREQ: param = Hz per encoder tick)
    add_default("Jog_A",    100, 10);      // VFO A Tune (ZZFA), 10 Hz/tick
    add_default("Pitch_A",  100, 100);     // VFO A Tune (ZZFA), 100 Hz/tick
    add_default("Vol_A",    501, 0);       // AF Gain (ZZAG)
    add_default("Treble_A", 613, 0);       // Filter High (ZZFH)
    add_default("Medium_A", 612, 0);       // Filter Low (ZZFL)
    add_default("Play_A",   434, 0);       // MOX toggle (ZZTX)
    add_default("CUE_A",    434, 0);       // MOX toggle (ZZTX)
    add_default("Listen_A", 507, 0);       // Mute toggle (ZZMA)
    add_default("Sync_A",  1209, 0);       // TUN toggle (ZZTU)
    add_default("Load_A",   219, 0);       // Band Up (ZZBU)

    // N1-N8 -> Bands
    add_default("N1_A",     202, 0);       // 160m
    add_default("N2_A",     203, 0);       // 80m
    add_default("N3_A",     205, 0);       // 40m
    add_default("N4_A",     207, 0);       // 20m
    add_default("N5_A",     208, 0);       // 17m
    add_default("N6_A",     209, 0);       // 15m
    add_default("N7_A",     210, 0);       // 12m
    add_default("N8_A",     211, 0);       // 10m

    // Crossfader -> Drive
    add_default("XFader",   421, 0);       // Drive Level (ZZPC)

    // -- Deck B --
    add_default("Jog_B",    101, 10);      // VFO B Tune (ZZFB), 10 Hz step
    add_default("Pitch_B",  101, 100);     // VFO B Tune (ZZFB), 100 Hz step
    add_default("Vol_B",    501, 0);       // AF Gain (ZZAG)
    add_default("Play_B",   911, 0);       // Split toggle (ZZSP)

    // FWD/RWD
    add_default("FWD_A",    424, 0);       // VFO A Step Up (ZZSB)
    add_default("RWD_A",    423, 0);       // VFO A Step Down (ZZSA)
    add_default("FWD_B",    219, 0);       // Band Up (ZZBU)
    add_default("RWD_B",    216, 0);       // Band Down (ZZBD)

    ESP_LOGI(TAG, "Default mappings loaded (%d entries)", s_mapping_count);
}

// ===================================================================
// SPIFFS persistence
// ===================================================================

esp_err_t mapping_engine_save(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return ESP_ERR_NO_MEM;

    for (int i = 0; i < s_mapping_count; i++) {
        const mapping_entry_t *e = &s_mappings[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "c", e->control_name);
        cJSON_AddNumberToObject(obj, "id", e->command_id);
        if (e->param != 0) {
            cJSON_AddNumberToObject(obj, "p", e->param);
        }
        cJSON_AddItemToArray(arr, obj);
        // Debug: log filter width entries specifically
        const thetis_cmd_t *dbcmd = cmd_db_find(e->command_id);
        if (dbcmd && dbcmd->exec_type == CMD_CAT_FILTER_WIDTH) {
            ESP_LOGI(TAG, "FW_DBG SAVE: '%s' -> cmd_id=%d (%s) param=%d",
                     e->control_name, e->command_id, dbcmd->name, (int)e->param);
        }
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) return ESP_ERR_NO_MEM;

    FILE *f = fopen(MAPPINGS_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", MAPPINGS_PATH);
        free(json);
        return ESP_FAIL;
    }

    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, f);
    fclose(f);
    free(json);

    if (written != len) {
        ESP_LOGE(TAG, "Short write to %s (%d/%d)", MAPPINGS_PATH, (int)written, (int)len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved %d mappings to %s (%d bytes)", s_mapping_count, MAPPINGS_PATH, (int)len);
    return ESP_OK;
}

esp_err_t mapping_engine_load(void)
{
    struct stat st;
    if (stat(MAPPINGS_PATH, &st) != 0) {
        ESP_LOGI(TAG, "No user mappings file at %s", MAPPINGS_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *f = fopen(MAPPINGS_PATH, "r");
    if (!f) return ESP_FAIL;

    char *json = malloc(st.st_size + 1);
    if (!json) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t read_len = fread(json, 1, st.st_size, f);
    fclose(f);
    json[read_len] = '\0';

    cJSON *arr = cJSON_Parse(json);
    free(json);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        ESP_LOGW(TAG, "Invalid JSON in %s", MAPPINGS_PATH);
        return ESP_ERR_INVALID_ARG;
    }

    // Overlay user mappings on top of defaults (don't clear — defaults already loaded)
    int user_count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        cJSON *c = cJSON_GetObjectItem(item, "c");
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *p = cJSON_GetObjectItem(item, "p");

        if (c && cJSON_IsString(c) && id && cJSON_IsNumber(id)) {
            uint16_t cmd_id = (uint16_t)id->valuedouble;

            // Verify command exists in DB
            const thetis_cmd_t *dbcmd = cmd_db_find(cmd_id);
            if (!dbcmd) {
                ESP_LOGW(TAG, "Unknown command ID %d for %s, skipping", cmd_id, c->valuestring);
                continue;
            }

            mapping_entry_t entry = {0};
            strncpy(entry.control_name, c->valuestring, sizeof(entry.control_name) - 1);
            entry.command_id = cmd_id;
            entry.param = (p && cJSON_IsNumber(p)) ? (int32_t)p->valuedouble : 0;
            // Debug: log filter width entries specifically
            if (dbcmd->exec_type == CMD_CAT_FILTER_WIDTH) {
                ESP_LOGI(TAG, "FW_DBG LOAD: '%s' -> cmd_id=%d (%s) param=%d",
                         entry.control_name, entry.command_id, dbcmd->name, (int)entry.param);
            }
            mapping_engine_set(&entry);  // Overwrites existing or appends
            user_count++;
        }
    }

    cJSON_Delete(arr);
    ESP_LOGI(TAG, "Overlaid %d user mappings from %s (total %d)", user_count, MAPPINGS_PATH, s_mapping_count);
    return ESP_OK;
}

// ===================================================================
// Public API
// ===================================================================

esp_err_t mapping_engine_init(void)
{
    // Always start from defaults, then overlay user mappings on top
    mapping_engine_reset_defaults();
    mapping_engine_load();  // Overlays user customizations (no-op if no file)

    ESP_LOGI(TAG, "Mapping engine initialized (%d mappings, %d commands in DB)",
             s_mapping_count, (int)CMD_DB_COUNT);
    return ESP_OK;
}

void mapping_engine_on_control(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value)
{
    // --- Learn mode: capture the first control that changes ---
    if (s_learn_active) {
        // Check timeout
        int64_t now = esp_timer_get_time();
        if ((now - s_learn_start_us) > LEARN_TIMEOUT_US) {
            s_learn_active = false;
            ESP_LOGI(TAG, "Learn mode timed out");
            return;
        }

        // Ignore button releases
        if (control_type == DJ_CTRL_BUTTON && new_value == 0) return;

        // For dials/encoders, require meaningful change
        if (control_type != DJ_CTRL_BUTTON) {
            int8_t d = encoder_delta(old_value, new_value);
            if (d == 0) return;
        }

        // Create mapping
        const thetis_cmd_t *cmd = cmd_db_find(s_learn_command_id);
        if (cmd) {
            mapping_entry_t entry = {0};
            strncpy(entry.control_name, name, sizeof(entry.control_name) - 1);
            entry.command_id = s_learn_command_id;
            // Set sensible default param for freq commands
            if (cmd->exec_type == CMD_CAT_FREQ) {
                if (control_type == DJ_CTRL_ENCODER) entry.param = 10;
                else entry.param = 100;
            }
            mapping_engine_set(&entry);
            esp_err_t save_ret = mapping_engine_save();

            ESP_LOGI(TAG, "Learned: %s -> [%d] %s (exec_type=%d, save=%s)",
                     name, cmd->id, cmd->name, cmd->exec_type,
                     save_ret == ESP_OK ? "OK" : "FAIL");

            if (s_learn_cb) {
                s_learn_cb(name, cmd->id, cmd->name);
            }
        }

        s_learn_active = false;
        return;
    }

    // --- Normal dispatch ---
    mapping_entry_t *m = find_mapping(name);
    if (!m) return;

    const thetis_cmd_t *cmd = cmd_db_find(m->command_id);
    if (!cmd) return;

    execute_command(cmd, name, control_type, old_value, new_value, m->param);

    // Update LED to reflect toggle state
    uint8_t led_note = find_led_note(name);
    if (led_note > 0 && cmd->exec_type == CMD_CAT_TOGGLE) {
        bool *state = find_toggle(cmd->id, cmd->cat_cmd);
        if (state) {
            dj_led_set(led_note, *state);
        }
    }
}

const mapping_entry_t *mapping_engine_get_table(int *count)
{
    if (count) *count = s_mapping_count;
    return s_mappings;
}

esp_err_t mapping_engine_set(const mapping_entry_t *entry)
{
    mapping_entry_t *existing = find_mapping(entry->control_name);
    if (existing) {
        memcpy(existing, entry, sizeof(mapping_entry_t));
        return ESP_OK;
    }
    if (s_mapping_count >= MAX_MAPPINGS) return ESP_ERR_NO_MEM;
    memcpy(&s_mappings[s_mapping_count++], entry, sizeof(mapping_entry_t));
    return ESP_OK;
}

esp_err_t mapping_engine_remove(const char *control_name)
{
    for (int i = 0; i < s_mapping_count; i++) {
        if (strcmp(s_mappings[i].control_name, control_name) == 0) {
            for (int j = i; j < s_mapping_count - 1; j++) {
                s_mappings[j] = s_mappings[j + 1];
            }
            s_mapping_count--;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

// ===================================================================
// Learn mode
// ===================================================================

void mapping_engine_start_learn(uint16_t command_id)
{
    const thetis_cmd_t *cmd = cmd_db_find(command_id);
    if (!cmd) {
        ESP_LOGW(TAG, "Learn: unknown command ID %d", command_id);
        return;
    }
    s_learn_command_id = command_id;
    s_learn_start_us = esp_timer_get_time();
    s_learn_active = true;
    ESP_LOGI(TAG, "Learn mode started for [%d] %s", cmd->id, cmd->name);
}

bool mapping_engine_is_learning(void)
{
    if (!s_learn_active) return false;
    // Auto-expire
    if ((esp_timer_get_time() - s_learn_start_us) > LEARN_TIMEOUT_US) {
        s_learn_active = false;
        return false;
    }
    return true;
}

void mapping_engine_cancel_learn(void)
{
    s_learn_active = false;
    ESP_LOGI(TAG, "Learn mode cancelled");
}

void mapping_engine_set_learn_callback(mapping_learn_callback_t cb)
{
    s_learn_cb = cb;
}

void mapping_engine_set_cat_callback(mapping_cat_callback_t cb)
{
    s_cat_cb = cb;
}

// ===================================================================
// CAT response handler — sync VFO freq and tuning step from Thetis
// ===================================================================

void mapping_engine_on_cat_response(const char *cmd, const char *value)
{
    if (!value || value[0] == '\0') return;

    if (strcmp(cmd, "ZZFA") == 0) {
        long f = atol(value);
        ESP_LOGI(TAG, "VFO_DBG RESPONSE ZZFA: raw='%s' parsed=%ld (prev=%ld synced=%d)",
                 value, f, s_vfo_a.freq, s_vfo_a.synced);
        if (f > 0) {
            s_vfo_a.freq = f;
            s_vfo_a.synced = true;
        }
    } else if (strcmp(cmd, "ZZFB") == 0) {
        long f = atol(value);
        ESP_LOGI(TAG, "VFO_DBG RESPONSE ZZFB: raw='%s' parsed=%ld (prev=%ld synced=%d)",
                 value, f, s_vfo_b.freq, s_vfo_b.synced);
        if (f > 0) {
            s_vfo_b.freq = f;
            s_vfo_b.synced = true;
        }
    } else if (strcmp(cmd, "ZZAC") == 0) {
        int idx = atoi(value);
        if (idx >= 0 && idx < (int)STEP_TABLE_SIZE) {
            s_tune_step_hz = s_step_table[idx];
            ESP_LOGI(TAG, "Sync tune step = %d Hz (index %d)", s_tune_step_hz, idx);
        }
    } else if (strcmp(cmd, "ZZFH") == 0) {
        s_filter.hi = atoi(value);
        s_filter.synced = true;
        s_filter.width = 0;  // reset so next tick re-derives from actual edges
        ESP_LOGI(TAG, "FW_DBG Sync ZZFH: filter_hi=%d raw='%s' (synced=%d, lo=%d)",
                 s_filter.hi, value, s_filter.synced, s_filter.lo);
    } else if (strcmp(cmd, "ZZFL") == 0) {
        s_filter.lo = atoi(value);
        s_filter.synced = true;
        s_filter.width = 0;  // reset so next tick re-derives from actual edges
        ESP_LOGI(TAG, "FW_DBG Sync ZZFL: filter_lo=%d raw='%s' (synced=%d, hi=%d)",
                 s_filter.lo, value, s_filter.synced, s_filter.hi);
    }

    // Generic toggle sync: if response matches a tracked toggle, update state + LED
    for (int i = 0; i < s_toggle_count; i++) {
        if (s_toggles[i].cat_cmd[0] != '\0' && strcmp(cmd, s_toggles[i].cat_cmd) == 0) {
            bool new_state = (atoi(value) != 0);
            if (s_toggles[i].state != new_state) {
                s_toggles[i].state = new_state;
                update_toggle_led(s_toggles[i].id, new_state);
                ESP_LOGI(TAG, "Sync toggle %s = %d", cmd, new_state);
            }
            break;
        }
    }

    // Generic SET value sync: update tracked encoder-set values from Thetis responses
    for (int i = 0; i < s_set_count; i++) {
        const thetis_cmd_t *sc = cmd_db_find(s_set_state[i].cmd_id);
        if (sc && strcmp(cmd, sc->cat_cmd) == 0) {
            s_set_state[i].value = atoi(value);
            ESP_LOGI(TAG, "Sync SET %s = %d", cmd, s_set_state[i].value);
            break;
        }
    }
}

void mapping_engine_request_sync(void)
{
    ESP_LOGI(TAG, "Requesting VFO/step/filter/toggle sync from Thetis");
    s_vfo_a.synced = false;
    s_vfo_b.synced = false;
    s_filter.synced = false;
    cat_client_send("ZZFA;");
    cat_client_send("ZZFB;");
    // Set tuning step to 10 Hz (index 2), then query back to confirm
    cat_client_send("ZZAC02;");
    cat_client_send("ZZAC;");
    // Query filter edges for FILTER_WIDTH exec type
    cat_client_send("ZZFH;");
    cat_client_send("ZZFL;");

    // Query all tracked toggle states
    for (int i = 0; i < s_toggle_count; i++) {
        if (s_toggles[i].cat_cmd[0] != '\0') {
            char buf[8];
            snprintf(buf, sizeof(buf), "%s;", s_toggles[i].cat_cmd);
            cat_client_send(buf);
        }
    }
}
