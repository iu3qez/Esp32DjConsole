#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CONFIG_NVS_NAMESPACE "djconfig"

// Keys
#define CFG_KEY_WIFI_SSID    "wifi_ssid"
#define CFG_KEY_WIFI_PASS    "wifi_pass"
#define CFG_KEY_DEBUG_LEVEL  "debug_lvl"

esp_err_t config_get_str(const char *key, char *out, size_t max_len);
esp_err_t config_set_str(const char *key, const char *value);
esp_err_t config_get_u16(const char *key, uint16_t *out);
esp_err_t config_set_u16(const char *key, uint16_t value);
esp_err_t config_get_u8(const char *key, uint8_t *out);
esp_err_t config_set_u8(const char *key, uint8_t value);
char *config_get_blob(const char *key, size_t *out_len);
esp_err_t config_set_blob(const char *key, const char *data, size_t len);
esp_err_t config_erase_key(const char *key);
