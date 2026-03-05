#include "midi_output.h"
#include "tusb.h"
#include "esp_log.h"

static const char *TAG = "midi_out";

esp_err_t midi_output_init(void) {
    ESP_LOGI(TAG, "MIDI output ready");
    return ESP_OK;
}

esp_err_t midi_output_send_cc(uint8_t channel, uint8_t cc_num, uint8_t value) {
    uint8_t msg[3] = { (uint8_t)(MIDI_CC | (channel & 0x0F)), cc_num & 0x7F, value & 0x7F };
    if (!tud_midi_n_mounted(0)) return ESP_ERR_INVALID_STATE;
    return tud_midi_n_stream_write(0, 0, msg, 3) ? ESP_OK : ESP_FAIL;
}

esp_err_t midi_output_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    uint8_t msg[3] = { (uint8_t)(MIDI_NOTE_ON | (channel & 0x0F)), note & 0x7F, velocity & 0x7F };
    if (!tud_midi_n_mounted(0)) return ESP_ERR_INVALID_STATE;
    return tud_midi_n_stream_write(0, 0, msg, 3) ? ESP_OK : ESP_FAIL;
}

esp_err_t midi_output_send_note_off(uint8_t channel, uint8_t note) {
    uint8_t msg[3] = { (uint8_t)(MIDI_NOTE_OFF | (channel & 0x0F)), note & 0x7F, 0 };
    if (!tud_midi_n_mounted(0)) return ESP_ERR_INVALID_STATE;
    return tud_midi_n_stream_write(0, 0, msg, 3) ? ESP_OK : ESP_FAIL;
}
