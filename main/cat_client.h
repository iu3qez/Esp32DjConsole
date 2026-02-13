#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * CAT (Computer Aided Transceiver) TCP client for Thetis SDR.
 *
 * Kenwood-compatible with ZZ extended commands over TCP.
 * Default port: 31001 (from TCPIPcatServer.cs).
 *
 * Command format: "ZZXXparams;" or "ZZxx;" (query)
 * Response format: "ZZXXvalue;" or "?;" (error)
 *
 * Key ZZ commands (from CATParser.cs):
 *   ZZFA - VFO A frequency (11 digits, Hz)
 *   ZZFB - VFO B frequency (11 digits, Hz)
 *   ZZMD - Mode (2 digits: 00=LSB,01=USB,03=CW,04=FM,05=AM,06=DIGL,09=DIGU)
 *   ZZAG - Audio gain/volume (3 digits, 0-100)
 *   ZZPC - TX power/drive (3 digits, 0-100)
 *   ZZTX - Transmit (1 digit: 0=RX, 1=TX)
 *   ZZTU - Tune (1 digit: 0=off, 1=on)
 *   ZZSM - S-meter (query: "ZZSM0;" response: "ZZSM0xxx;")
 *   ZZFI - RX filter low (5 digits, Hz)
 *   ZZFH - RX filter high (5 digits, Hz)
 *   ZZSP - Split (1 digit: 0=off, 1=on)
 *   ZZMA - Mute RX1 (1 digit: 0=off, 1=on)
 *   ZZPS - Power on/off (1 digit: 0=off, 1=on)
 *
 * Standard Kenwood (also supported):
 *   FA - VFO A frequency
 *   FB - VFO B frequency
 *   MD - Mode
 *   AG - Audio gain
 *   PC - Power
 *   TX - Transmit
 *   SM - S-meter
 */

/**
 * CAT connection state.
 */
typedef enum {
    CAT_STATE_DISCONNECTED = 0,
    CAT_STATE_CONNECTING,
    CAT_STATE_CONNECTED,
    CAT_STATE_ERROR,
} cat_state_t;

/**
 * CAT mode codes (from CATParser.cs ZZMD).
 */
typedef enum {
    CAT_MODE_LSB  = 0,
    CAT_MODE_USB  = 1,
    CAT_MODE_DSB  = 2,
    CAT_MODE_CW   = 3,
    CAT_MODE_FM   = 4,
    CAT_MODE_AM   = 5,
    CAT_MODE_DIGL = 6,
    CAT_MODE_CWR  = 7,
    CAT_MODE_SPEC = 8,
    CAT_MODE_DIGU = 9,
    CAT_MODE_SAM  = 10,
    CAT_MODE_DRM  = 11,
} cat_mode_t;

/**
 * Callback fired when CAT connection state changes.
 */
typedef void (*cat_state_callback_t)(cat_state_t new_state);

/**
 * Callback fired when a CAT response is received.
 * @param cmd    The command prefix (e.g., "ZZFA", "ZZSM")
 * @param value  The value portion (e.g., "00014074000", "0120")
 */
typedef void (*cat_response_callback_t)(const char *cmd, const char *value);

/**
 * CAT client configuration.
 */
typedef struct {
    char     host[64];
    uint16_t port;           // Default 31001
    cat_state_callback_t    state_cb;
    cat_response_callback_t response_cb;
} cat_client_config_t;

/**
 * Initialize and start the CAT TCP client.
 * Spawns a FreeRTOS task that connects and auto-reconnects.
 */
esp_err_t cat_client_init(const cat_client_config_t *config);

/**
 * Stop and clean up the CAT client.
 */
void cat_client_stop(void);

/**
 * Get the current connection state.
 */
cat_state_t cat_client_get_state(void);

/**
 * Send a raw CAT command. The trailing ';' is appended if missing.
 * Thread-safe. Response comes via callback.
 */
esp_err_t cat_client_send(const char *cmd);

// Convenience functions:

esp_err_t cat_client_set_vfo_a(long freq_hz);
esp_err_t cat_client_set_vfo_b(long freq_hz);
esp_err_t cat_client_set_mode(cat_mode_t mode);
esp_err_t cat_client_set_volume(int level);     // 0-100
esp_err_t cat_client_set_drive(int power);      // 0-100
esp_err_t cat_client_set_ptt(bool tx);
esp_err_t cat_client_set_tune(bool tune);
esp_err_t cat_client_set_mute(bool mute);
esp_err_t cat_client_set_split(bool split);
esp_err_t cat_client_query_smeter(void);        // Response via callback
esp_err_t cat_client_query_vfo_a(void);
esp_err_t cat_client_query_vfo_b(void);
esp_err_t cat_client_query_mode(void);
