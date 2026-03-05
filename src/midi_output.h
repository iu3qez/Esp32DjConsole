#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    MIDI_NOTE_OFF     = 0x80,
    MIDI_NOTE_ON      = 0x90,
    MIDI_CC           = 0xB0,
    MIDI_PROG_CHANGE  = 0xC0,
} midi_msg_type_t;

esp_err_t midi_output_init(void);
esp_err_t midi_output_send_cc(uint8_t channel, uint8_t cc_num, uint8_t value);
esp_err_t midi_output_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
esp_err_t midi_output_send_note_off(uint8_t channel, uint8_t note);
