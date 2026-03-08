#pragma once
#include "esp_err.h"

esp_err_t midi_input_init(void);
void midi_input_poll(void);
