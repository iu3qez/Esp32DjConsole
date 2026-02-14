#include "mapping_engine.h"
#include "cat_client.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "mapper";

// ===================================================================
// Thetis Command Database (~80 commands, static const in flash)
// ===================================================================

static const thetis_cmd_t s_cmd_db[] = {
    // ------ VFO (id 100-119) ------
    // Both use read-modify-write: query ZZFA/ZZFB, add delta*step, send absolute freq.
    // (Same logic as midi2cat ChangeFreqVfoA/B — see Midi2CatCommands.cs:1169-1241)
    { 100, "VFO A Tune",         CAT_VFO,   CMD_CAT_FREQ,   "ZZFA", NULL,  11, 0, 0 },
    { 101, "VFO B Tune",         CAT_VFO,   CMD_CAT_FREQ,   "ZZFB", NULL,  11, 0, 0 },
    { 102, "VFO A -> B",         CAT_VFO,   CMD_CAT_BUTTON, "ZZAB", NULL,   0, 0, 0 },
    { 103, "VFO B -> A",         CAT_VFO,   CMD_CAT_BUTTON, "ZZBA", NULL,   0, 0, 0 },
    { 104, "VFO Swap",           CAT_VFO,   CMD_CAT_BUTTON, "ZZVS", NULL,   0, 0, 0 },
    { 105, "VFO A Up 100kHz",    CAT_VFO,   CMD_CAT_BUTTON, "ZZAU", NULL,   0, 0, 0 },
    { 106, "VFO A Down 100kHz",  CAT_VFO,   CMD_CAT_BUTTON, "ZZAD", NULL,   0, 0, 0 },
    { 107, "VFO B Up 100kHz",    CAT_VFO,   CMD_CAT_BUTTON, "ZZBY", NULL,   0, 0, 0 },
    { 108, "VFO B Down 100kHz",  CAT_VFO,   CMD_CAT_BUTTON, "ZZBB", NULL,   0, 0, 0 },
    { 109, "VFO Sync",           CAT_VFO,   CMD_CAT_TOGGLE, "ZZSY", NULL,   1, 0, 1 },
    { 110, "Tuning Step Up",     CAT_VFO,   CMD_CAT_BUTTON, "ZZSU", NULL,   0, 0, 0 },
    { 111, "Tuning Step Down",   CAT_VFO,   CMD_CAT_BUTTON, "ZZSD", NULL,   0, 0, 0 },
    { 112, "Multi Step VFO A",   CAT_VFO,   CMD_CAT_WHEEL,  "UP", "DN",     0, 0, 0 },
    { 113, "Lock VFO A",         CAT_VFO,   CMD_CAT_TOGGLE, "ZZLA", NULL,   1, 0, 1 },
    { 114, "Lock VFO B",         CAT_VFO,   CMD_CAT_TOGGLE, "ZZLB", NULL,   1, 0, 1 },

    // ------ Band (id 200-219) ------
    { 200, "Band Up",            CAT_BAND,  CMD_CAT_BUTTON, "ZZBU", NULL,   0, 0, 0 },
    { 201, "Band Down",          CAT_BAND,  CMD_CAT_BUTTON, "ZZBD", NULL,   0, 0, 0 },
    { 202, "160m",               CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3, 160, 160 },
    { 203, "80m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  80,  80 },
    { 204, "60m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  60,  60 },
    { 205, "40m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  40,  40 },
    { 206, "30m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  30,  30 },
    { 207, "20m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  20,  20 },
    { 208, "17m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  17,  17 },
    { 209, "15m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  15,  15 },
    { 210, "12m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  12,  12 },
    { 211, "10m",                CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,  10,  10 },
    { 212, "6m",                 CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,   6,   6 },
    { 213, "2m",                 CAT_BAND,  CMD_CAT_BUTTON, "ZZBS", NULL,   3,   2,   2 },
    { 214, "RX2 Band Up",        CAT_BAND,  CMD_CAT_BUTTON, "ZZBE", NULL,   0, 0, 0 },
    { 215, "RX2 Band Down",      CAT_BAND,  CMD_CAT_BUTTON, "ZZBF", NULL,   0, 0, 0 },

    // ------ Mode (id 300-319) ------
    { 300, "Mode Next",          CAT_MODE,  CMD_CAT_BUTTON, "ZZMU", NULL,   0, 0, 0 },
    { 301, "Mode Prev",          CAT_MODE,  CMD_CAT_BUTTON, "ZZML", NULL,   0, 0, 0 },
    { 302, "LSB",                CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 0, 0 },
    { 303, "USB",                CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 1, 1 },
    { 304, "DSB",                CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 2, 2 },
    { 305, "CW Lower",          CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 3, 3 },
    { 306, "FM",                 CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 4, 4 },
    { 307, "AM",                 CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 5, 5 },
    { 308, "DIGL",               CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 6, 6 },
    { 309, "CW Upper",          CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 7, 7 },
    { 310, "SPEC",               CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 8, 8 },
    { 311, "DIGU",               CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 9, 9 },
    { 312, "SAM",                CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 10, 10 },
    { 313, "DRM",                CAT_MODE,  CMD_CAT_BUTTON, "ZZMD", NULL,   2, 11, 11 },
    { 314, "RX2 Mode Next",      CAT_MODE,  CMD_CAT_BUTTON, "ZZMV", NULL,   0, 0, 0 },
    { 315, "RX2 Mode Prev",      CAT_MODE,  CMD_CAT_BUTTON, "ZZMW", NULL,   0, 0, 0 },

    // ------ TX (id 400-419) ------
    { 400, "MOX On/Off",         CAT_TX,    CMD_CAT_TOGGLE, "ZZTX", NULL,   1, 0, 1 },
    { 401, "Tune On/Off",        CAT_TX,    CMD_CAT_TOGGLE, "ZZTU", NULL,   1, 0, 1 },
    { 402, "Tuner On/Off",       CAT_TX,    CMD_CAT_TOGGLE, "ZZOC", NULL,   1, 0, 1 },
    { 403, "VOX On/Off",         CAT_TX,    CMD_CAT_TOGGLE, "ZZVE", NULL,   1, 0, 1 },
    { 404, "Two Tone On/Off",    CAT_TX,    CMD_CAT_TOGGLE, "ZZUT", NULL,   1, 0, 1 },
    { 405, "PS On/Off",          CAT_TX,    CMD_CAT_TOGGLE, "ZZLM", NULL,   1, 0, 1 },
    { 406, "Toggle TX VFO",      CAT_TX,    CMD_CAT_BUTTON, "ZZSA", NULL,   0, 0, 0 },
    { 407, "Tuner Bypass",       CAT_TX,    CMD_CAT_TOGGLE, "ZZOD", NULL,   1, 0, 1 },
    { 408, "External PA On/Off", CAT_TX,    CMD_CAT_TOGGLE, "ZZPE", NULL,   1, 0, 1 },

    // ------ Audio (id 500-529) ------
    { 500, "AF Gain",            CAT_AUDIO, CMD_CAT_SET,    "ZZAG", NULL,   3, 0, 100 },
    { 501, "RX2 Volume",         CAT_AUDIO, CMD_CAT_SET,    "ZZVA", NULL,   3, 0, 100 },
    { 502, "Mute On/Off",        CAT_AUDIO, CMD_CAT_TOGGLE, "ZZMA", NULL,   1, 0, 1 },
    { 503, "RX2 Mute On/Off",    CAT_AUDIO, CMD_CAT_TOGGLE, "ZZMB", NULL,   1, 0, 1 },
    { 504, "MON On/Off",         CAT_AUDIO, CMD_CAT_TOGGLE, "ZZMO", NULL,   1, 0, 1 },
    { 505, "Drive Level",        CAT_AUDIO, CMD_CAT_SET,    "ZZPC", NULL,   3, 0, 100 },
    { 506, "Mic Gain",           CAT_AUDIO, CMD_CAT_SET,    "ZZMG", NULL,   3, 0, 100 },
    { 507, "RX1 AGC Level",      CAT_AUDIO, CMD_CAT_SET,    "ZZAR", NULL,   3, 0, 120 },
    { 508, "RX2 AGC Level",      CAT_AUDIO, CMD_CAT_SET,    "ZZAS", NULL,   3, 0, 120 },
    { 509, "DX Level",           CAT_AUDIO, CMD_CAT_SET,    "ZZDX", NULL,   3, 0, 100 },

    // ------ Filter (id 600-619) ------
    { 600, "Filter High",        CAT_FILTER, CMD_CAT_SET,   "ZZFH", NULL,   5, 0, 20000 },
    { 601, "Filter Low",         CAT_FILTER, CMD_CAT_SET,   "ZZFI", NULL,   5, 0, 20000 },
    { 602, "Filter Wider",       CAT_FILTER, CMD_CAT_BUTTON,"ZZFW", NULL,   0, 0, 0 },
    { 603, "Filter Narrower",    CAT_FILTER, CMD_CAT_BUTTON,"ZZFN", NULL,   0, 0, 0 },
    { 604, "Filter High Wheel",  CAT_FILTER, CMD_CAT_WHEEL, "ZZHU", "ZZHD", 0, 0, 0 },
    { 605, "Filter Low Wheel",   CAT_FILTER, CMD_CAT_WHEEL, "ZZLU", "ZZLD", 0, 0, 0 },
    { 606, "RX2 Filter Wider",   CAT_FILTER, CMD_CAT_BUTTON,"ZZFV", NULL,   0, 0, 0 },
    { 607, "RX2 Filter Narrower",CAT_FILTER, CMD_CAT_BUTTON,"ZZFX", NULL,   0, 0, 0 },
    { 608, "TX Filter High Whl", CAT_FILTER, CMD_CAT_WHEEL, "ZZHW", "ZZHX", 0, 0, 0 },
    { 609, "TX Filter Low Whl",  CAT_FILTER, CMD_CAT_WHEEL, "ZZLG", "ZZLH", 0, 0, 0 },

    // ------ NR/NB (id 700-729) ------
    { 700, "NB1 On/Off",         CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNA", NULL,  1, 0, 1 },
    { 701, "NB2 On/Off",         CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNB", NULL,  1, 0, 1 },
    { 702, "NR On/Off",          CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNR", NULL,  1, 0, 1 },
    { 703, "NR2 On/Off",         CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNS", NULL,  1, 0, 1 },
    { 704, "Auto Notch On/Off",  CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNT", NULL,  1, 0, 1 },
    { 705, "SNB On/Off",         CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNN", NULL,  1, 0, 1 },
    { 706, "Binaural On/Off",    CAT_NR_NB, CMD_CAT_TOGGLE, "ZZBI", NULL,  1, 0, 1 },
    { 707, "RX2 NB1 On/Off",     CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNC", NULL,  1, 0, 1 },
    { 708, "RX2 NB2 On/Off",     CAT_NR_NB, CMD_CAT_TOGGLE, "ZZND", NULL,  1, 0, 1 },
    { 709, "RX2 ANF On/Off",     CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNU", NULL,  1, 0, 1 },
    { 710, "RX2 NR1 On/Off",     CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNV", NULL,  1, 0, 1 },
    { 711, "RX2 NR2 On/Off",     CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNW", NULL,  1, 0, 1 },
    { 712, "RX2 SNB On/Off",     CAT_NR_NB, CMD_CAT_TOGGLE, "ZZNO", NULL,  1, 0, 1 },

    // ------ AGC (id 800-819) ------
    { 800, "AGC Mode Up",        CAT_AGC,   CMD_CAT_BUTTON, "ZZGU", NULL,   0, 0, 0 },
    { 801, "AGC Mode Down",      CAT_AGC,   CMD_CAT_BUTTON, "ZZGD", NULL,   0, 0, 0 },
    { 802, "AGC Level",          CAT_AGC,   CMD_CAT_SET,    "ZZGT", NULL,   3, 0, 120 },
    { 803, "RX2 AGC Mode Up",    CAT_AGC,   CMD_CAT_BUTTON, "ZZGE", NULL,   0, 0, 0 },
    { 804, "RX2 AGC Mode Down",  CAT_AGC,   CMD_CAT_BUTTON, "ZZGL", NULL,   0, 0, 0 },

    // ------ Split/RIT/XIT (id 900-919) ------
    { 900, "Split On/Off",       CAT_SPLIT_RIT, CMD_CAT_TOGGLE, "ZZSP", NULL, 1, 0, 1 },
    { 901, "Quick Split",        CAT_SPLIT_RIT, CMD_CAT_BUTTON, "ZZQS", NULL, 0, 0, 0 },
    { 902, "RIT On/Off",         CAT_SPLIT_RIT, CMD_CAT_TOGGLE, "ZZRT", NULL, 1, 0, 1 },
    { 903, "XIT On/Off",         CAT_SPLIT_RIT, CMD_CAT_TOGGLE, "ZZXT", NULL, 1, 0, 1 },
    { 904, "RIT Clear",          CAT_SPLIT_RIT, CMD_CAT_BUTTON, "ZZRC", NULL, 0, 0, 0 },
    { 905, "XIT Clear",          CAT_SPLIT_RIT, CMD_CAT_BUTTON, "ZZXC", NULL, 0, 0, 0 },
    { 906, "RIT Tune",           CAT_SPLIT_RIT, CMD_CAT_WHEEL,  "ZZRU", "ZZRD", 0, 0, 0 },
    { 907, "XIT Tune",           CAT_SPLIT_RIT, CMD_CAT_WHEEL,  "ZZXU", "ZZXD", 0, 0, 0 },

    // ------ CW (id 1000-1019) ------
    { 1000, "CW Speed",          CAT_CW,    CMD_CAT_SET,    "ZZCS", NULL,   2, 1, 60 },
    { 1001, "CW Break-In On/Off",CAT_CW,    CMD_CAT_TOGGLE, "ZZCB", NULL,  1, 0, 1 },
    { 1002, "CW Sidetone Freq",  CAT_CW,    CMD_CAT_SET,    "ZZCI", NULL,   4, 100, 2000 },
    { 1003, "CW Speed Inc",      CAT_CW,    CMD_CAT_WHEEL,  "ZZCU", "ZZCD", 0, 0, 0 },
    { 1004, "CW QSK On/Off",     CAT_CW,    CMD_CAT_TOGGLE, "ZZCF", NULL,  1, 0, 1 },

    // ------ Misc (id 1100-1139) ------
    { 1100, "Squelch On/Off",    CAT_MISC,  CMD_CAT_TOGGLE, "ZZSQ", NULL,  1, 0, 1 },
    { 1101, "Compander On/Off",  CAT_MISC,  CMD_CAT_TOGGLE, "ZZCP", NULL,  1, 0, 1 },
    { 1102, "RX2 On/Off",        CAT_MISC,  CMD_CAT_TOGGLE, "ZZRX", NULL,  1, 0, 1 },
    { 1103, "Click Tune On/Off", CAT_MISC,  CMD_CAT_TOGGLE, "ZZCT", NULL,  1, 0, 1 },
    { 1104, "Power On/Off",      CAT_MISC,  CMD_CAT_TOGGLE, "ZZPS", NULL,  1, 0, 1 },
    { 1105, "Squelch Level",     CAT_MISC,  CMD_CAT_SET,    "ZZSV", NULL,  3, 0, 160 },
    { 1106, "RX EQ On/Off",      CAT_MISC,  CMD_CAT_TOGGLE, "ZZER", NULL,  1, 0, 1 },
    { 1107, "TX EQ On/Off",      CAT_MISC,  CMD_CAT_TOGGLE, "ZZET", NULL,  1, 0, 1 },
    { 1108, "DEXP On/Off",       CAT_MISC,  CMD_CAT_TOGGLE, "ZZDA", NULL,  1, 0, 1 },
    { 1109, "Diversity On/Off",  CAT_MISC,  CMD_CAT_TOGGLE, "ZZDB", NULL,  1, 0, 1 },
    { 1110, "Display Pan Down",  CAT_MISC,  CMD_CAT_BUTTON, "ZZPD", NULL,  0, 0, 0 },
    { 1111, "Zoom Inc",          CAT_MISC,  CMD_CAT_WHEEL,  "ZZZA", "ZZZB", 0, 0, 0 },
    { 1112, "Display Mode Next", CAT_MISC,  CMD_CAT_BUTTON, "ZZDU", NULL,  0, 0, 0 },
    { 1113, "VAC On/Off",        CAT_MISC,  CMD_CAT_TOGGLE, "ZZVC", NULL,  1, 0, 1 },
    { 1114, "Quick Mode Save",   CAT_MISC,  CMD_CAT_BUTTON, "ZZQM", NULL,  0, 0, 0 },
    { 1115, "Quick Mode Restore",CAT_MISC,  CMD_CAT_BUTTON, "ZZQR", NULL,  0, 0, 0 },
    { 1116, "RX2 Squelch On/Off",CAT_MISC,  CMD_CAT_TOGGLE, "ZZSZ", NULL,  1, 0, 1 },
    { 1117, "RX2 CTUN On/Off",   CAT_MISC,  CMD_CAT_TOGGLE, "ZZCO", NULL,  1, 0, 1 },
    { 1118, "APF On/Off",        CAT_MISC,  CMD_CAT_TOGGLE, "ZZAP", NULL,  1, 0, 1 },
};

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

// Toggle state tracker (one bit per command ID - use a simple array)
#define TOGGLE_SLOTS 32
static struct { uint16_t id; bool state; } s_toggles[TOGGLE_SLOTS];
static int s_toggle_count = 0;

// VFO freq tracking for FREQ exec type (synced from Thetis via CAT responses)
static long s_vfo_a_freq = 0;  // 0 = not yet synced from Thetis
static long s_vfo_b_freq = 0;
static bool s_vfo_a_synced = false;
static bool s_vfo_b_synced = false;

// Tuning step from Thetis ZZAC response (index 0-25 -> Hz)
static const int s_step_table[] = {
    1, 2, 10, 25, 50, 100, 250, 500,           // 0-7
    1000, 2000, 2500, 5000, 6250, 9000,         // 8-13
    10000, 12500, 15000, 20000, 25000, 30000,   // 14-19
    50000, 100000, 250000, 500000, 1000000, 10000000  // 20-25
};
#define STEP_TABLE_SIZE (sizeof(s_step_table) / sizeof(s_step_table[0]))
static int s_tune_step_hz = 10;  // default 10 Hz, updated from ZZAC if supported

// Velocity scaling: slow wheel = 1x step, fast wheel = up to max_multiplier x step
#define VELOCITY_MAX_MULTIPLIER 10
#define VELOCITY_FAST_THRESHOLD  5   // encoder delta >= this = max speed

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
    // If already on boundary and going down, the integer division handled one step
    if (direction < 0 && freq % step != 0)
        direction++;  // off boundary, division already stepped down
    snapped += (long)direction * step;
    return snapped;
}

// Velocity-based step multiplier: scales 1x..VELOCITY_MAX_MULTIPLIER based on delta
static int velocity_multiplier(int abs_delta)
{
    if (abs_delta <= 1) return 1;
    if (abs_delta >= VELOCITY_FAST_THRESHOLD)
        return VELOCITY_MAX_MULTIPLIER;
    // Linear interpolation between 1 and max
    return 1 + (abs_delta - 1) * (VELOCITY_MAX_MULTIPLIER - 1) / (VELOCITY_FAST_THRESHOLD - 1);
}

static bool *find_toggle(uint16_t cmd_id)
{
    for (int i = 0; i < s_toggle_count; i++) {
        if (s_toggles[i].id == cmd_id) return &s_toggles[i].state;
    }
    if (s_toggle_count < TOGGLE_SLOTS) {
        s_toggles[s_toggle_count].id = cmd_id;
        s_toggles[s_toggle_count].state = false;
        return &s_toggles[s_toggle_count++].state;
    }
    return NULL;
}

// ===================================================================
// CAT command execution
// ===================================================================

static void notify_cat(const char *control_name, const thetis_cmd_t *cmd, const char *cat_str)
{
    if (s_cat_cb) s_cat_cb(control_name, cmd->name, cmd->exec_type, cat_str);
}

static void execute_command(const thetis_cmd_t *cmd, const char *control_name,
                            dj_control_type_t ctrl_type,
                            uint8_t old_val, uint8_t new_val, int32_t param)
{
    char buf[32];

    switch (cmd->exec_type) {

    case CMD_CAT_BUTTON:
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
        break;

    case CMD_CAT_TOGGLE: {
        if (ctrl_type == DJ_CTRL_BUTTON && new_val == 0) return;
        bool *state = find_toggle(cmd->id);
        if (!state) return;
        *state = !(*state);
        snprintf(buf, sizeof(buf), "%s%0*d;", cmd->cat_cmd,
                 cmd->value_digits, *state ? cmd->value_max : cmd->value_min);
        cat_client_send(buf);
        notify_cat(control_name, cmd, buf);
        ESP_LOGI(TAG, "TOGGLE [%s] -> %s (state=%d)", cmd->name, buf, *state);
        break;
    }

    case CMD_CAT_SET: {
        // Scale 0-255 to value_min..value_max
        int range = cmd->value_max - cmd->value_min;
        int val = cmd->value_min + (new_val * range) / 255;
        snprintf(buf, sizeof(buf), "%s%0*d;", cmd->cat_cmd,
                 cmd->value_digits, val);
        cat_client_send(buf);
        notify_cat(control_name, cmd, buf);
        ESP_LOGD(TAG, "SET [%s] raw=%d -> val=%d -> %s", cmd->name, new_val, val, buf);
        break;
    }

    case CMD_CAT_FREQ: {
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
        if (delta == 0) break;

        // Step: use Thetis step (from ZZAC), scaled by velocity
        int base_step = s_tune_step_hz;
        int mult = velocity_multiplier(abs(delta));
        int step_hz = base_step * mult;

        long *freq = NULL;
        bool synced = false;
        if (strcmp(cmd->cat_cmd, "ZZFA") == 0) {
            freq = &s_vfo_a_freq;
            synced = s_vfo_a_synced;
        } else if (strcmp(cmd->cat_cmd, "ZZFB") == 0) {
            freq = &s_vfo_b_freq;
            synced = s_vfo_b_synced;
        }
        if (!freq) break;
        if (!synced) {
            ESP_LOGW(TAG, "FREQ [%s] skipped — VFO not synced from Thetis yet", cmd->name);
            break;
        }

        int direction = (delta > 0) ? 1 : -1;
        *freq = snap_tune(*freq, step_hz, direction);
        if (*freq < 100000) *freq = 100000;
        if (*freq > 54000000) *freq = 54000000;
        snprintf(buf, sizeof(buf), "%s%011ld;", cmd->cat_cmd, *freq);
        cat_client_send(buf);
        notify_cat(control_name, cmd, buf);
        ESP_LOGD(TAG, "FREQ [%s] delta=%d step=%d (x%d) -> %ld Hz",
                 cmd->name, delta, step_hz, mult, *freq);
        break;
    }

    case CMD_CAT_WHEEL: {
        int8_t delta = 0;
        if (ctrl_type == DJ_CTRL_ENCODER) {
            delta = encoder_delta(old_val, new_val);
        } else if (ctrl_type == DJ_CTRL_BUTTON) {
            if (new_val == 0) return;
            delta = 1;
        }
        // Send inc command for positive delta, dec for negative
        const char *c = (delta > 0) ? cmd->cat_cmd : cmd->cat_cmd2;
        if (!c) break;
        int count = abs(delta);
        if (count > 10) count = 10;  // Sanity limit
        for (int i = 0; i < count; i++) {
            snprintf(buf, sizeof(buf), "%s;", c);
            cat_client_send(buf);
        }
        notify_cat(control_name, cmd, buf);
        ESP_LOGD(TAG, "WHEEL [%s] delta=%d x%d", cmd->name, delta, count);
        break;
    }
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
    add_default("Jog_A",    100, 10);      // VFO A Tune, 10 Hz/tick
    add_default("Pitch_A",  100, 100);     // VFO A Tune, 100 Hz/tick
    add_default("Vol_A",    500, 0);       // AF Gain
    add_default("Treble_A", 600, 0);       // Filter High
    add_default("Medium_A", 601, 0);       // Filter Low
    add_default("Play_A",   400, 0);       // MOX toggle
    add_default("CUE_A",    400, 0);       // MOX toggle
    add_default("Listen_A", 502, 0);       // Mute toggle
    add_default("Sync_A",   401, 0);       // Tune toggle
    add_default("Load_A",   300, 0);       // Mode Next

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
    add_default("XFader",   505, 0);       // Drive Level

    // -- Deck B --
    add_default("Jog_B",    101, 10);      // VFO B Tune, 10 Hz step
    add_default("Pitch_B",  101, 100);     // VFO B Tune, 100 Hz step
    add_default("Vol_B",    500, 0);       // AF Gain
    add_default("Play_B",   900, 0);       // Split toggle

    // FWD/RWD -> VFO jump buttons
    add_default("FWD_A",    105, 0);       // VFO A Up 100kHz
    add_default("RWD_A",    106, 0);       // VFO A Down 100kHz
    add_default("FWD_B",    107, 0);       // VFO B Up 100kHz
    add_default("RWD_B",    108, 0);       // VFO B Down 100kHz

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
        ESP_LOGI(TAG, "No mappings file at %s", MAPPINGS_PATH);
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

    s_mapping_count = 0;
    memset(s_mappings, 0, sizeof(s_mappings));

    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (s_mapping_count >= MAX_MAPPINGS) break;

        mapping_entry_t *e = &s_mappings[s_mapping_count];
        cJSON *c = cJSON_GetObjectItem(item, "c");
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *p = cJSON_GetObjectItem(item, "p");

        if (c && cJSON_IsString(c) && id && cJSON_IsNumber(id)) {
            strncpy(e->control_name, c->valuestring, sizeof(e->control_name) - 1);
            e->command_id = (uint16_t)id->valuedouble;
            e->param = (p && cJSON_IsNumber(p)) ? (int32_t)p->valuedouble : 0;

            // Verify command exists in DB
            if (cmd_db_find(e->command_id)) {
                s_mapping_count++;
            } else {
                ESP_LOGW(TAG, "Unknown command ID %d for %s, skipping", e->command_id, e->control_name);
            }
        }
    }

    cJSON_Delete(arr);
    ESP_LOGI(TAG, "Loaded %d mappings from %s", s_mapping_count, MAPPINGS_PATH);
    return ESP_OK;
}

// ===================================================================
// Public API
// ===================================================================

esp_err_t mapping_engine_init(void)
{
    if (mapping_engine_load() != ESP_OK) {
        mapping_engine_reset_defaults();
        mapping_engine_save();  // Write defaults to SPIFFS
    }

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
            mapping_engine_save();

            ESP_LOGI(TAG, "Learned: %s -> [%d] %s", name, cmd->id, cmd->name);

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
        if (f > 0) {
            s_vfo_a_freq = f;
            s_vfo_a_synced = true;
            ESP_LOGI(TAG, "Sync VFO A = %ld Hz", f);
        }
    } else if (strcmp(cmd, "ZZFB") == 0) {
        long f = atol(value);
        if (f > 0) {
            s_vfo_b_freq = f;
            s_vfo_b_synced = true;
            ESP_LOGI(TAG, "Sync VFO B = %ld Hz", f);
        }
    } else if (strcmp(cmd, "ZZAC") == 0) {
        int idx = atoi(value);
        if (idx >= 0 && idx < (int)STEP_TABLE_SIZE) {
            s_tune_step_hz = s_step_table[idx];
            ESP_LOGI(TAG, "Sync tune step = %d Hz (index %d)", s_tune_step_hz, idx);
        }
    }
}

void mapping_engine_request_sync(void)
{
    ESP_LOGI(TAG, "Requesting VFO/step sync from Thetis (ZZFA, ZZFB, ZZAC)");
    s_vfo_a_synced = false;
    s_vfo_b_synced = false;
    cat_client_send("ZZFA;");
    cat_client_send("ZZFB;");
    // Set tuning step to lowest (index 0 = 1 Hz), then query back to confirm
    cat_client_send("ZZAC00;");
    cat_client_send("ZZAC;");
}
