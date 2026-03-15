#include "event_engine.h"
#include "midi_output.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "event_eng";

static midi_mapping_t s_mappings[MAX_MIDI_MAPPINGS];
static int s_mapping_count = 0;

// Default mappings matching sample.ino convention:
// Buttons (0-45) -> Note On/Off with note = controlIndex
// Dials/Sliders/Encoders (46-58) -> CC with cc_num = controlIndex
// All on MIDI channel 1 (index 0)
static void load_defaults(void) {
    s_mapping_count = 0;

    // Buttons -> Note On/Off, note number = control index
    for (uint8_t i = 0; i < 46 && s_mapping_count < MAX_MIDI_MAPPINGS; i++) {
        s_mappings[s_mapping_count++] = (midi_mapping_t){
            .control_id   = i,
            .midi_channel = 0,
            .midi_type    = MIDI_NOTE_ON,
            .midi_param   = i,
            .scale        = 1.0f,
        };
    }

    // Dials, sliders, encoders -> CC, cc number = control index
    for (uint8_t i = 46; i < 59 && s_mapping_count < MAX_MIDI_MAPPINGS; i++) {
        s_mappings[s_mapping_count++] = (midi_mapping_t){
            .control_id   = i,
            .midi_channel = 0,
            .midi_type    = MIDI_CC,
            .midi_param   = i,
            .scale        = 0.5f,
        };
    }

    ESP_LOGI(TAG, "Loaded %d default mappings", s_mapping_count);
}

esp_err_t event_engine_init(void) {
    esp_err_t err = event_engine_load();
    if (err != ESP_OK) {
        load_defaults();
    }
    return ESP_OK;
}

void event_engine_process(const control_event_t *event) {
    // Find mapping for this control
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].control_id != event->control_id) continue;

        const midi_mapping_t *m = &s_mappings[i];

        switch (m->midi_type) {
        case MIDI_NOTE_ON:
            if (event->type == CTRL_BUTTON) {
                if (event->value) {
                    midi_output_send_note_on(m->midi_channel, m->midi_param, 127);
                } else {
                    midi_output_send_note_off(m->midi_channel, m->midi_param);
                }
            }
            break;

        case MIDI_CC: {
            uint8_t cc_val;
            if (event->type == CTRL_ENCODER) {
                // Encoder: compute direction from delta
                int diff = (int)event->value - (int)event->old_value;
                if (diff > 127) diff -= 256;
                if (diff < -128) diff += 256;
                if (diff == 0) break;
                cc_val = (diff > 0) ? 1 : 127;
            } else {
                // Dial/slider: scale raw value
                int scaled = (int)(event->value * m->scale);
                if (scaled < 0) scaled = 0;
                if (scaled > 127) scaled = 127;
                cc_val = (uint8_t)scaled;
            }
            midi_output_send_cc(m->midi_channel, m->midi_param, cc_val);
            break;
        }

        default:
            break;
        }
        return;  // found mapping, done
    }
}

const midi_mapping_t *event_engine_get_mappings(int *count) {
    if (count) *count = s_mapping_count;
    return s_mappings;
}

esp_err_t event_engine_set_mapping(const midi_mapping_t *mapping) {
    // Update existing or add new
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].control_id == mapping->control_id) {
            s_mappings[i] = *mapping;
            return ESP_OK;
        }
    }
    if (s_mapping_count >= MAX_MIDI_MAPPINGS) return ESP_ERR_NO_MEM;
    s_mappings[s_mapping_count++] = *mapping;
    return ESP_OK;
}

esp_err_t event_engine_remove_mapping(uint8_t control_id) {
    for (int i = 0; i < s_mapping_count; i++) {
        if (s_mappings[i].control_id == control_id) {
            memmove(&s_mappings[i], &s_mappings[i + 1],
                    (s_mapping_count - i - 1) * sizeof(midi_mapping_t));
            s_mapping_count--;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t event_engine_save(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("midi_map", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(nvs, "mappings", s_mappings, s_mapping_count * sizeof(midi_mapping_t));
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "count", (uint8_t)s_mapping_count);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved %d mappings to NVS", s_mapping_count);
    }
    return err;
}

esp_err_t event_engine_load(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("midi_map", NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    uint8_t count = 0;
    err = nvs_get_u8(nvs, "count", &count);
    if (err != ESP_OK || count > MAX_MIDI_MAPPINGS) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    size_t blob_size = 0;
    err = nvs_get_blob(nvs, "mappings", NULL, &blob_size);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }

    size_t new_size = count * sizeof(midi_mapping_t);
    size_t old_entry_size = 4;
    size_t old_size = count * old_entry_size;

    if (blob_size == new_size) {
        err = nvs_get_blob(nvs, "mappings", s_mappings, &blob_size);
    } else if (blob_size == old_size) {
        ESP_LOGI(TAG, "Migrating %d mappings from old format", count);
        uint8_t old_buf[MAX_MIDI_MAPPINGS * 4];
        size_t read_size = old_size;
        err = nvs_get_blob(nvs, "mappings", old_buf, &read_size);
        if (err == ESP_OK) {
            for (int i = 0; i < count; i++) {
                s_mappings[i].control_id   = old_buf[i * 4 + 0];
                s_mappings[i].midi_channel = old_buf[i * 4 + 1];
                s_mappings[i].midi_type    = old_buf[i * 4 + 2];
                s_mappings[i].midi_param   = old_buf[i * 4 + 3];
                s_mappings[i].scale        = 0.5f;
            }
        }
    } else {
        ESP_LOGW(TAG, "Unknown blob size %d for %d entries, loading defaults", blob_size, count);
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    nvs_close(nvs);

    if (err == ESP_OK) {
        s_mapping_count = count;
        ESP_LOGI(TAG, "Loaded %d mappings from NVS", s_mapping_count);
    }
    return err;
}
