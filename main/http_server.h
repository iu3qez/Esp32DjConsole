#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "usb_dj_host.h"

/**
 * HTTP Server - serves Svelte SPA from SPIFFS, REST API, and WebSocket live updates.
 *
 * REST API:
 *   GET  /api/status           - System status (USB, TCI/CAT, radio state, heap)
 *   GET  /api/config           - Current configuration (WiFi, protocol, hosts/ports)
 *   PUT  /api/config           - Update configuration (JSON body)
 *   GET  /api/mappings         - Current mapping table (JSON array)
 *   PUT  /api/mappings         - Replace entire mapping table (JSON array)
 *   POST /api/mappings/reset   - Reset mappings to defaults
 *
 * WebSocket:
 *   /ws  - Live push updates (JSON messages):
 *     {"type":"control","name":"Play_A","value":1,"old":0}
 *     {"type":"radio","vfo_a":14074000,"mode":"USB",...}
 *     {"type":"status","usb":true,"tci":"ready","cat":"disconnected"}
 *
 * Static files:
 *   (all other paths) - Served from SPIFFS /www partition (Svelte SPA build output)
 *   Falls back to /index.html for SPA client-side routing
 */

/**
 * Initialize and start the HTTP server on port 80.
 * Mounts the SPIFFS /www partition for static file serving.
 *
 * @return ESP_OK on success
 */
esp_err_t http_server_init(void);

/**
 * Stop the HTTP server and unmount SPIFFS.
 */
void http_server_stop(void);

/**
 * Broadcast a JSON message to all connected WebSocket clients.
 * Thread-safe. Message is copied internally.
 *
 * @param json  Null-terminated JSON string
 */
void http_server_ws_broadcast(const char *json);

/**
 * Notify WebSocket clients of a DJ control change.
 * Call this from the USB control callback.
 */
void http_server_notify_control(
    const char *name,
    dj_control_type_t control_type,
    uint8_t old_value,
    uint8_t new_value);

/**
 * Notify WebSocket clients of a radio state change.
 * Call this when TCI/CAT notifications arrive.
 */
void http_server_notify_radio(void);

/**
 * Notify WebSocket clients of a connection status change.
 * Call this when USB/TCI/CAT state changes.
 */
void http_server_notify_status(void);
