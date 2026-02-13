#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * TCI (Transceiver Control Interface) WebSocket client for Thetis SDR.
 *
 * Protocol: WebSocket text frames on port 50001 (default).
 * Command format: "command:arg1,arg2,...;"
 * No-arg commands: "command;"
 *
 * After WebSocket upgrade, Thetis sends initialization data:
 *   protocol:Thetis,1.8;  device:...; trx_count:2; channels_count:2;
 *   Then full radio state (vfo, modulation, drive, mute, etc.)
 *   Then "ready;"
 *
 * Commands we send (from Thetis TCIServer.cs parseTextFrame):
 *   vfo:rx,chan,freq;      - Set VFO frequency (Hz)
 *   vfo:rx,chan;            - Query VFO frequency
 *   modulation:rx,MODE;    - Set mode (USB,LSB,CW,AM,FM,DIGU,DIGL...)
 *   trx:rx,true/false;     - PTT on/off
 *   drive:rx,power;        - TX drive power
 *   tune:rx,true/false;    - Tune on/off
 *   mute:true/false;       - Global mute
 *   rx_mute:rx,true/false; - Per-receiver mute
 *   split_enable:rx,bool;  - Split mode
 *   start;                 - Power on radio
 *   stop;                  - Power off radio
 *   rx_filter_band:rx,low,high; - (sent by server, not parsed as input)
 *
 * Notifications from Thetis (pushed to us):
 *   vfo:rx,chan,freq;
 *   modulation:rx,MODE;
 *   trx:rx,bool;
 *   drive:rx,power;
 *   mute:bool;
 *   rx_filter_band:rx,low,high;
 *   start; / stop;
 *   ready;
 */

/**
 * TCI connection state.
 */
typedef enum {
    TCI_STATE_DISCONNECTED = 0,
    TCI_STATE_CONNECTING,
    TCI_STATE_WEBSOCKET_UPGRADE,
    TCI_STATE_CONNECTED,
    TCI_STATE_READY,    // received "ready;" from Thetis
    TCI_STATE_ERROR,
} tci_state_t;

/**
 * Radio state received from Thetis via TCI notifications.
 */
typedef struct {
    long     vfo_a_freq;     // VFO A frequency in Hz
    long     vfo_b_freq;     // VFO B frequency in Hz
    char     mode[8];        // Current mode string (USB, LSB, CW, etc.)
    int      drive;          // TX drive level 0-100
    bool     tx;             // true = transmitting
    bool     mute;           // Global mute
    int      filter_low;     // RX filter low edge (Hz)
    int      filter_high;    // RX filter high edge (Hz)
    bool     power_on;       // Radio powered on
} tci_radio_state_t;

/**
 * Callback fired when TCI connection state changes.
 */
typedef void (*tci_state_callback_t)(tci_state_t new_state);

/**
 * Callback fired when a TCI notification is received from Thetis.
 * The raw message is provided for parsing/forwarding to web GUI.
 *
 * @param cmd   Command name (e.g., "vfo", "modulation", "trx")
 * @param args  Argument string after the colon (e.g., "0,0,14074000")
 */
typedef void (*tci_notification_callback_t)(const char *cmd, const char *args);

/**
 * TCI client configuration.
 */
typedef struct {
    char     host[64];       // Thetis host IP
    uint16_t port;           // Thetis TCI port (default 50001)
    tci_state_callback_t        state_cb;
    tci_notification_callback_t notify_cb;
} tci_client_config_t;

/**
 * Initialize and start the TCI WebSocket client.
 * Spawns a FreeRTOS task that connects and auto-reconnects.
 */
esp_err_t tci_client_init(const tci_client_config_t *config);

/**
 * Stop and clean up the TCI client.
 */
void tci_client_stop(void);

/**
 * Get the current connection state.
 */
tci_state_t tci_client_get_state(void);

/**
 * Get a read-only pointer to the cached radio state.
 */
const tci_radio_state_t *tci_client_get_radio_state(void);

/**
 * Send a raw TCI command string. The trailing ';' is appended if missing.
 * Thread-safe.
 *
 * @param cmd  TCI command, e.g., "vfo:0,0,14074000"
 */
esp_err_t tci_client_send(const char *cmd);

// Convenience functions for common commands:

esp_err_t tci_client_set_vfo(int rx, int chan, long freq_hz);
esp_err_t tci_client_set_mode(int rx, const char *mode);
esp_err_t tci_client_set_ptt(int rx, bool tx);
esp_err_t tci_client_set_drive(int rx, int power);
esp_err_t tci_client_set_tune(int rx, bool tune);
esp_err_t tci_client_set_mute(bool mute);
esp_err_t tci_client_set_split(int rx, bool split);
