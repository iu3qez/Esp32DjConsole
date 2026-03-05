#pragma once

#include "esp_err.h"
#include "usb_dj_host.h"

/**
 * HTTP Server - serves Svelte SPA from SPIFFS, REST API, and WebSocket live updates.
 *
 * REST API:
 *   GET  /api/status              - System status (USB, MIDI, heap)
 *   GET  /api/config              - Current configuration (WiFi)
 *   PUT  /api/config              - Update configuration (JSON body)
 *   GET  /api/mappings            - Current MIDI mapping table (JSON array)
 *   PUT  /api/mappings            - Replace entire mapping table
 *   POST /api/mappings/reset      - Reset mappings to defaults
 *
 * WebSocket:
 *   /ws  - Server->Client:
 *       {"type":"control","name":"Play_A","ctrl":0,"old":0,"new":1}
 *       {"type":"status","usb":true,"midi":true,"heap":123456}
 *
 * Static files:
 *   All other paths - Served from SPIFFS /www partition, SPA fallback to index.html
 */

esp_err_t http_server_init(void);
void http_server_stop(void);
void http_server_ws_broadcast(const char *json);

void http_server_notify_control(
    const char *name,
    dj_control_type_t control_type,
    uint8_t old_value,
    uint8_t new_value);

void http_server_notify_status(void);
