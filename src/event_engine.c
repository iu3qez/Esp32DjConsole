#include "event_engine.h"
#include "midi_output.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "event_eng";

static midi_mapping_t s_mappings[MAX_MIDI_MAPPINGS];
static int s_mapping_count = 0;

// Default mappings: buttons -> Note On/Off, sliders -> CC, encoders -> CC relative
static void load_defaults(void) {
    s_mapping_count = 0;

    // Map first 46 controls (buttons) as notes on channel 0
    for (uint8_t i = 0; i < 46 && s_mapping_count < MAX_MIDI_MAPPINGS; i++) {
        s_mappings[s_mapping_count++] = (midi_mapping_t){
            .control_id   = i,
            .midi_channel = 0,
            .midi_type    = MIDI_NOTE_ON,
            .midi_param   = i,
        };
    }

    // Map controls 46-54 (sliders/dials) as CC on channel 0
    for (uint8_t i = 46; i < 55 && s_mapping_count < MAX_MIDI_MAPPINGS; i++) {
        s_mappings[s_mapping_count++] = (midi_mapping_t){
            .control_id   = i,
            .midi_channel = 0,
            .midi_type    = MIDI_CC,
            .midi_param   = (uint8_t)(i - 46 + 14),  // CC14-CC22
        };
    }

    // Map controls 55-58 (encoders) as CC on channel 0
    for (uint8_t i = 55; i < 59 && s_mapping_count < MAX_MIDI_MAPPINGS; i++) {
        s_mappings[s_mapping_count++] = (midi_mapping_t){
            .control_id   = i,
            .midi_channel = 0,
            .midi_type    = MIDI_CC,
            .midi_param   = (uint8_t)(i - 55 + 24),  // CC24-CC27
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

        case MIDI_CC:
            if (event->type == CTRL_SLIDER) {
                // Scale 0-255 to 0-127
                uint8_t cc_val = (uint8_t)(event->value >> 1);
                midi_output_send_cc(m->midi_channel, m->midi_param, cc_val);
            } else if (event->type == CTRL_ENCODER) {
                // Relative: positive = 0x01-0x3F, negative = 0x41-0x7F
                int16_t delta = event->value;
                uint8_t cc_val;
                if (delta > 0) {
                    cc_val = (uint8_t)(delta > 63 ? 63 : delta);
                } else {
                    cc_val = (uint8_t)(64 + (-delta > 63 ? 63 : -delta));
                }
                midi_output_send_cc(m->midi_channel, m->midi_param, cc_val);
            } else if (event->type == CTRL_BUTTON) {
                // Button as CC toggle: 127 on press, 0 on release
                midi_output_send_cc(m->midi_channel, m->midi_param, event->value ? 127 : 0);
            }
            break;

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

    size_t blob_size = count * sizeof(midi_mapping_t);
    err = nvs_get_blob(nvs, "mappings", s_mappings, &blob_size);
    nvs_close(nvs);

    if (err == ESP_OK) {
        s_mapping_count = count;
        ESP_LOGI(TAG, "Loaded %d mappings from NVS", s_mapping_count);
    }
    return err;
}
