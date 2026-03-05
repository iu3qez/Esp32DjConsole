#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    CTRL_BUTTON,
    CTRL_SLIDER,
    CTRL_ENCODER,
} control_type_t;

typedef struct {
    uint8_t control_id;
    control_type_t type;
    int16_t value;
} control_event_t;

typedef struct {
    uint8_t control_id;
    uint8_t midi_channel;
    uint8_t midi_type;      // MIDI_CC, MIDI_NOTE_ON, etc.
    uint8_t midi_param;     // CC number or note number
    float   scale;          // multiplier applied to CC value, default 0.5
} midi_mapping_t;

#define MAX_MIDI_MAPPINGS 64

esp_err_t event_engine_init(void);
void event_engine_process(const control_event_t *event);
const midi_mapping_t *event_engine_get_mappings(int *count);
esp_err_t event_engine_set_mapping(const midi_mapping_t *mapping);
esp_err_t event_engine_remove_mapping(uint8_t control_id);
esp_err_t event_engine_save(void);
esp_err_t event_engine_load(void);
