#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define HERCULES_VID    0x06F8
#define HERCULES_PID    0xB105

#define DJ_STATE_SIZE   38

typedef enum {
    DJ_CTRL_BUTTON  = 0,
    DJ_CTRL_DIAL    = 1,
    DJ_CTRL_ENCODER = 2,
} dj_control_type_t;

typedef void (*dj_control_callback_t)(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value);

esp_err_t usb_dj_host_init(dj_control_callback_t callback);
bool usb_dj_host_is_connected(void);
const uint8_t *usb_dj_host_get_state(void);
