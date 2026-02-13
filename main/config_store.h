#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * NVS-based persistent configuration store.
 * Namespace: "djconfig"
 */

#define CONFIG_NVS_NAMESPACE "djconfig"

// Keys
#define CFG_KEY_WIFI_SSID    "wifi_ssid"
#define CFG_KEY_WIFI_PASS    "wifi_pass"
#define CFG_KEY_TCI_HOST     "tci_host"
#define CFG_KEY_TCI_PORT     "tci_port"
#define CFG_KEY_CAT_HOST     "cat_host"
#define CFG_KEY_CAT_PORT     "cat_port"
#define CFG_KEY_PROTOCOL     "protocol"    // "tci" or "cat"
#define CFG_KEY_MAPPINGS     "mappings"    // JSON blob
#define CFG_KEY_DEBUG_LEVEL  "debug_lvl"

/**
 * Get a string value from NVS. Returns ESP_ERR_NOT_FOUND if key doesn't exist.
 */
esp_err_t config_get_str(const char *key, char *out, size_t max_len);

/**
 * Set a string value in NVS.
 */
esp_err_t config_set_str(const char *key, const char *value);

/**
 * Get a uint16 value from NVS.
 */
esp_err_t config_get_u16(const char *key, uint16_t *out);

/**
 * Set a uint16 value in NVS.
 */
esp_err_t config_set_u16(const char *key, uint16_t value);

/**
 * Get a uint8 value from NVS.
 */
esp_err_t config_get_u8(const char *key, uint8_t *out);

/**
 * Set a uint8 value in NVS.
 */
esp_err_t config_set_u8(const char *key, uint8_t value);

/**
 * Get a large blob (for JSON mappings). Caller must free the returned buffer.
 * Returns NULL if key doesn't exist.
 */
char *config_get_blob(const char *key, size_t *out_len);

/**
 * Set a large blob (for JSON mappings).
 */
esp_err_t config_set_blob(const char *key, const char *data, size_t len);

/**
 * Erase a key from NVS.
 */
esp_err_t config_erase_key(const char *key);
