#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialize WiFi in STA mode using credentials from NVS.
 * Falls back to AP mode (SSID: "DJConsole-Setup") if no credentials stored
 * or connection fails after retries.
 *
 * Blocks until connected (STA) or AP started.
 * Returns ESP_OK on success.
 */
esp_err_t wifi_manager_init(void);

/**
 * Store WiFi credentials in NVS and restart WiFi in STA mode.
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/**
 * Returns true if connected to an AP in STA mode.
 */
bool wifi_manager_is_connected(void);
