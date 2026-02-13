#include "usb_debug.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "usb_dbg";

static int      s_debug_level = 1;   // Default: control changes only
static uint32_t s_update_count = 0;
static uint32_t s_change_count = 0;

// Keep a copy of the previous raw state for diff display
static uint8_t s_prev_raw[DJ_STATE_SIZE];
static bool    s_have_prev = false;

void usb_debug_set_level(int level)
{
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    s_debug_level = level;
    ESP_LOGI(TAG, "Debug level set to %d", s_debug_level);
}

int usb_debug_get_level(void)
{
    return s_debug_level;
}

void usb_debug_control_cb(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value)
{
    s_change_count++;

    if (s_debug_level == 0) return;

    const char *type_str;
    switch (control_type) {
    case DJ_CTRL_BUTTON:  type_str = "BTN"; break;
    case DJ_CTRL_DIAL:    type_str = "DIA"; break;
    case DJ_CTRL_ENCODER: type_str = "ENC"; break;
    default:              type_str = "???"; break;
    }

    // Level 1+: Log control name and value
    ESP_LOGI(TAG, "[%s] %-20s idx=%2d  %3d -> %3d  (0x%02X -> 0x%02X)",
             type_str, name, control_index, old_value, new_value, old_value, new_value);

    // Level 2+: Show which raw bytes changed
    if (s_debug_level >= 2 && s_have_prev) {
        const uint8_t *state = usb_dj_host_get_state();
        if (state) {
            usb_debug_diff_states(s_prev_raw, state);
        }
    }

    // Level 3: Full hex dump handled in the raw state callback hook
}

void usb_debug_dump_state(void)
{
    const uint8_t *state = usb_dj_host_get_state();
    if (!state) {
        ESP_LOGW(TAG, "No device connected, no state to dump");
        return;
    }

    char hex_line[DJ_STATE_SIZE * 3 + 1];
    char ascii_line[DJ_STATE_SIZE + 1];
    int pos = 0;

    for (int i = 0; i < DJ_STATE_SIZE; i++) {
        pos += snprintf(hex_line + pos, sizeof(hex_line) - pos, "%02X ", state[i]);
        ascii_line[i] = (state[i] >= 0x20 && state[i] <= 0x7E) ? state[i] : '.';
    }
    ascii_line[DJ_STATE_SIZE] = '\0';

    ESP_LOGI(TAG, "=== State Dump (%lu updates, %lu changes) ===",
             (unsigned long)s_update_count, (unsigned long)s_change_count);

    // Print in rows of 16 bytes with offset
    for (int row = 0; row < DJ_STATE_SIZE; row += 16) {
        char line[128];
        int lpos = snprintf(line, sizeof(line), "%02X: ", row);
        int count = (DJ_STATE_SIZE - row < 16) ? (DJ_STATE_SIZE - row) : 16;

        for (int i = 0; i < count; i++) {
            lpos += snprintf(line + lpos, sizeof(line) - lpos, "%02X ", state[row + i]);
        }
        // Pad if last row is short
        for (int i = count; i < 16; i++) {
            lpos += snprintf(line + lpos, sizeof(line) - lpos, "   ");
        }
        lpos += snprintf(line + lpos, sizeof(line) - lpos, " |");
        for (int i = 0; i < count; i++) {
            uint8_t c = state[row + i];
            line[lpos++] = (c >= 0x20 && c <= 0x7E) ? c : '.';
        }
        line[lpos++] = '|';
        line[lpos] = '\0';

        ESP_LOGI(TAG, "%s", line);
    }
}

void usb_debug_diff_states(const uint8_t *old_state, const uint8_t *new_state)
{
    char diff_buf[256];
    int pos = 0;
    int changes = 0;

    for (int i = 0; i < DJ_STATE_SIZE; i++) {
        if (old_state[i] != new_state[i]) {
            pos += snprintf(diff_buf + pos, sizeof(diff_buf) - pos,
                            " [%02d] %02X->%02X", i, old_state[i], new_state[i]);
            changes++;
            if (pos > 200) break;  // Don't overflow buffer
        }
    }

    if (changes > 0) {
        ESP_LOGI(TAG, "  DIFF (%d bytes):%s", changes, diff_buf);
    }
}

uint32_t usb_debug_get_update_count(void)
{
    return s_update_count;
}

uint32_t usb_debug_get_change_count(void)
{
    return s_change_count;
}

void usb_debug_reset_counters(void)
{
    s_update_count = 0;
    s_change_count = 0;
    s_have_prev = false;
    ESP_LOGI(TAG, "Debug counters reset");
}

void usb_debug_raw_state_cb(const uint8_t *raw_data, int length)
{
    s_update_count++;

    // Level 3: full hex dump on every packet
    if (s_debug_level >= 3) {
        char hex[DJ_STATE_SIZE * 3 + 1];
        int pos = 0;
        int dump_len = (length < DJ_STATE_SIZE) ? length : DJ_STATE_SIZE;
        for (int i = 0; i < dump_len; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", raw_data[i]);
        }
        ESP_LOGI(TAG, "RAW [%d bytes]: %s", length, hex);
    }

    // Save previous state for diff display
    if (s_have_prev && s_debug_level >= 2) {
        // Diff will be printed from control_cb
    }
    memcpy(s_prev_raw, raw_data, (length < DJ_STATE_SIZE) ? length : DJ_STATE_SIZE);
    s_have_prev = true;
}
