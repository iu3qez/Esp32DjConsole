#include "midi_input.h"
#include "tusb.h"
#include "esp_log.h"

static const char *TAG = "midi_in";

esp_err_t midi_input_init(void) {
    ESP_LOGI(TAG, "MIDI input ready (drain mode)");
    return ESP_OK;
}

// TEMPORARY: drain all incoming MIDI data to prevent Windows driver hang.
// Data is read and discarded. Future implementation will interpret
// incoming MIDI messages (e.g. for LED feedback control).
void midi_input_poll(void) {
    if (!tud_midi_n_mounted(0)) return;

    uint8_t buf[48];
    while (tud_midi_n_available(0, 0)) {
        tud_midi_n_stream_read(0, 0, buf, sizeof(buf));
    }
}
