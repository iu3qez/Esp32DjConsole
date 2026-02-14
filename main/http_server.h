#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "usb_dj_host.h"

/**
 * HTTP Server - serves Svelte SPA from SPIFFS, REST API, and WebSocket live updates.
 *
 * REST API:
 *   GET  /api/status              - System status (USB, CAT, heap)
 *   GET  /api/config              - Current configuration (WiFi, CAT host/port)
 *   PUT  /api/config              - Update configuration (JSON body)
 *   GET  /api/commands            - Thetis command database (for UI command browser)
 *   GET  /api/mappings            - Current mapping table (JSON array)
 *   PUT  /api/mappings            - Replace entire mapping table (JSON array)
 *   POST /api/mappings/reset      - Reset mappings to defaults
 *   GET  /api/mappings/download   - Download mappings.json as file attachment
 *   POST /api/mappings/upload     - Upload mappings.json, validate and reload
 *   POST /api/mappings/clear?c=X  - Remove mapping for control X
 *
 * WebSocket:
 *   /ws  - Bidirectional:
 *     Server->Client:
 *       {"type":"control","name":"Play_A","ctrl":0,"old":0,"new":1}
 *       {"type":"status","usb":true,"cat":"connected","heap":123456}
 *       {"type":"learned","control":"Jog_A","command_id":100,"command_name":"VFO A Tune"}
 *       {"type":"learn_timeout"}
 *     Client->Server:
 *       {"type":"learn","command_id":100}
 *       {"type":"learn_cancel"}
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
