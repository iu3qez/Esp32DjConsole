#include "tci_client.h"
#include "status_led.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "tci";

// ---------------------------------------------------------------------------
// WebSocket framing (minimal client implementation, RFC 6455)
// ---------------------------------------------------------------------------

// WebSocket opcodes
#define WS_OP_TEXT   0x1
#define WS_OP_CLOSE 0x8
#define WS_OP_PING  0x9
#define WS_OP_PONG  0xA

// Max TCI message size (ASCII text, typically < 200 bytes)
#define TCI_MAX_MSG_LEN  512
#define TCI_RX_BUF_SIZE  2048

// Reconnect intervals
#define RECONNECT_DELAY_MS    3000
#define CONNECT_TIMEOUT_MS    5000
#define WS_PING_INTERVAL_MS  20000

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static tci_client_config_t s_config;
static tci_state_t         s_state = TCI_STATE_DISCONNECTED;
static tci_radio_state_t   s_radio = {0};
static int                 s_sock = -1;
static TaskHandle_t        s_task_handle = NULL;
static SemaphoreHandle_t   s_send_mutex = NULL;
static bool                s_stop_requested = false;

// Receive buffer
static uint8_t s_rx_buf[TCI_RX_BUF_SIZE];

// Message assembly buffer (for accumulating text across frames)
static char s_msg_buf[TCI_MAX_MSG_LEN];
static int  s_msg_len = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void set_state(tci_state_t new_state)
{
    if (s_state != new_state) {
        ESP_LOGI(TAG, "State: %d -> %d", s_state, new_state);
        s_state = new_state;
        if (s_config.state_cb) {
            s_config.state_cb(new_state);
        }
    }
}

static int sock_send_all(const void *data, int len)
{
    const uint8_t *p = (const uint8_t *)data;
    int sent = 0;
    while (sent < len) {
        int n = send(s_sock, p + sent, len - sent, 0);
        if (n < 0) return -1;
        sent += n;
    }
    return sent;
}

// ---------------------------------------------------------------------------
// WebSocket upgrade handshake (client side)
// ---------------------------------------------------------------------------

// Generate a random 16-byte key, base64-encoded (24 chars)
static void generate_ws_key(char *out, size_t out_size)
{
    uint8_t raw[16];
    for (int i = 0; i < 16; i++) {
        raw[i] = esp_random() & 0xFF;
    }

    // Simple base64 encode (we only need it for the handshake)
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int j = 0;
    for (int i = 0; i < 16; i += 3) {
        uint32_t v = (raw[i] << 16);
        if (i + 1 < 16) v |= (raw[i + 1] << 8);
        if (i + 2 < 16) v |= raw[i + 2];

        out[j++] = b64[(v >> 18) & 0x3F];
        out[j++] = b64[(v >> 12) & 0x3F];
        out[j++] = (i + 1 < 16) ? b64[(v >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < 16) ? b64[v & 0x3F] : '=';
    }
    out[j] = '\0';
}

static esp_err_t do_ws_handshake(void)
{
    char ws_key[32];
    generate_ws_key(ws_key, sizeof(ws_key));

    char request[512];
    int len = snprintf(request, sizeof(request),
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        s_config.host, s_config.port, ws_key);

    if (sock_send_all(request, len) < 0) {
        ESP_LOGE(TAG, "Failed to send WS handshake");
        return ESP_FAIL;
    }

    // Read response (HTTP 101 Switching Protocols)
    int total = 0;
    int timeout_ticks = pdMS_TO_TICKS(CONNECT_TIMEOUT_MS);
    TickType_t start = xTaskGetTickCount();

    while (total < (int)sizeof(s_rx_buf) - 1) {
        if ((xTaskGetTickCount() - start) > timeout_ticks) {
            ESP_LOGE(TAG, "WS handshake timeout");
            return ESP_ERR_TIMEOUT;
        }

        int n = recv(s_sock, s_rx_buf + total, sizeof(s_rx_buf) - 1 - total, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(TAG, "WS handshake recv error: %d", errno);
            return ESP_FAIL;
        }
        if (n == 0) {
            ESP_LOGE(TAG, "Connection closed during handshake");
            return ESP_FAIL;
        }
        total += n;
        s_rx_buf[total] = '\0';

        // Check for end of HTTP headers
        if (strstr((char *)s_rx_buf, "\r\n\r\n")) {
            break;
        }
    }

    // Verify 101 response
    if (!strstr((char *)s_rx_buf, "101")) {
        ESP_LOGE(TAG, "WS upgrade rejected: %.100s", (char *)s_rx_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WebSocket upgrade successful");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// WebSocket frame send (client must mask)
// ---------------------------------------------------------------------------

static esp_err_t ws_send_text(const char *text, int text_len)
{
    if (s_sock < 0) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Frame header
    uint8_t header[10];
    int hdr_len = 0;

    header[0] = 0x80 | WS_OP_TEXT;  // FIN + TEXT

    // Client frames MUST be masked (bit 7 of byte 1)
    if (text_len < 126) {
        header[1] = 0x80 | text_len;
        hdr_len = 2;
    } else {
        header[1] = 0x80 | 126;
        header[2] = (text_len >> 8) & 0xFF;
        header[3] = text_len & 0xFF;
        hdr_len = 4;
    }

    // Masking key (4 random bytes)
    uint32_t mask_key = esp_random();
    uint8_t mask[4];
    memcpy(mask, &mask_key, 4);

    // Send header + mask key
    if (sock_send_all(header, hdr_len) < 0 ||
        sock_send_all(mask, 4) < 0) {
        xSemaphoreGive(s_send_mutex);
        return ESP_FAIL;
    }

    // Send masked payload
    uint8_t masked[TCI_MAX_MSG_LEN];
    int copy_len = (text_len < TCI_MAX_MSG_LEN) ? text_len : TCI_MAX_MSG_LEN;
    for (int i = 0; i < copy_len; i++) {
        masked[i] = text[i] ^ mask[i % 4];
    }

    esp_err_t ret = (sock_send_all(masked, copy_len) < 0) ? ESP_FAIL : ESP_OK;
    xSemaphoreGive(s_send_mutex);
    return ret;
}

static esp_err_t ws_send_pong(const uint8_t *data, int data_len)
{
    if (s_sock < 0) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t header[2];
    header[0] = 0x80 | WS_OP_PONG;
    header[1] = 0x80 | (data_len & 0x7F);

    uint32_t mask_key = esp_random();
    uint8_t mask[4];
    memcpy(mask, &mask_key, 4);

    sock_send_all(header, 2);
    sock_send_all(mask, 4);

    if (data_len > 0) {
        uint8_t masked[128];
        int copy = (data_len < 128) ? data_len : 128;
        for (int i = 0; i < copy; i++) {
            masked[i] = data[i] ^ mask[i % 4];
        }
        sock_send_all(masked, copy);
    }

    xSemaphoreGive(s_send_mutex);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// WebSocket frame receive
// Returns: payload length on success, 0 for control frames handled, -1 on error
// ---------------------------------------------------------------------------

static int ws_recv_frame(char *payload_out, int max_len, uint8_t *opcode_out)
{
    // Read first 2 bytes
    uint8_t hdr[2];
    int n = recv(s_sock, hdr, 2, MSG_WAITALL);
    if (n <= 0) return -1;
    if (n < 2) return -1;

    uint8_t opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (recv(s_sock, ext, 2, MSG_WAITALL) < 2) return -1;
        payload_len = (ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (recv(s_sock, ext, 8, MSG_WAITALL) < 8) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    // Server frames should NOT be masked, but handle if they are
    uint8_t mask[4] = {0};
    if (masked) {
        if (recv(s_sock, mask, 4, MSG_WAITALL) < 4) return -1;
    }

    // Read payload
    if (payload_len > (uint64_t)max_len) {
        ESP_LOGW(TAG, "Frame too large: %llu > %d, discarding", payload_len, max_len);
        // Drain the oversized frame
        int remaining = (int)payload_len;
        while (remaining > 0) {
            int chunk = (remaining < max_len) ? remaining : max_len;
            n = recv(s_sock, payload_out, chunk, MSG_WAITALL);
            if (n <= 0) return -1;
            remaining -= n;
        }
        *opcode_out = opcode;
        return 0;
    }

    int plen = (int)payload_len;
    if (plen > 0) {
        int total = 0;
        while (total < plen) {
            n = recv(s_sock, payload_out + total, plen - total, 0);
            if (n <= 0) return -1;
            total += n;
        }

        // Unmask if needed
        if (masked) {
            for (int i = 0; i < plen; i++) {
                payload_out[i] ^= mask[i % 4];
            }
        }
    }

    *opcode_out = opcode;

    // Handle control frames
    if (opcode == WS_OP_PING) {
        ws_send_pong((uint8_t *)payload_out, plen);
        return 0;  // Handled internally
    }
    if (opcode == WS_OP_CLOSE) {
        ESP_LOGW(TAG, "Received WS close frame");
        return -1;
    }

    return plen;
}

// ---------------------------------------------------------------------------
// TCI message parsing
// ---------------------------------------------------------------------------

static void parse_tci_notification(const char *cmd, const char *args)
{
    if (strcmp(cmd, "vfo") == 0 && args) {
        int rx, chan;
        long freq;
        if (sscanf(args, "%d,%d,%ld", &rx, &chan, &freq) == 3) {
            if (rx == 0 && chan == 0) s_radio.vfo_a_freq = freq;
            else if (rx == 0 && chan == 1) s_radio.vfo_b_freq = freq;
            ESP_LOGD(TAG, "VFO rx=%d ch=%d freq=%ld", rx, chan, freq);
        }
    } else if (strcmp(cmd, "modulation") == 0 && args) {
        int rx;
        char mode[8];
        if (sscanf(args, "%d,%7s", &rx, mode) == 2) {
            if (rx == 0) {
                strncpy(s_radio.mode, mode, sizeof(s_radio.mode) - 1);
            }
            ESP_LOGD(TAG, "Mode rx=%d: %s", rx, mode);
        }
    } else if (strcmp(cmd, "trx") == 0 && args) {
        int rx;
        char state_str[8];
        if (sscanf(args, "%d,%7s", &rx, state_str) == 2) {
            s_radio.tx = (strcmp(state_str, "true") == 0);
            ESP_LOGD(TAG, "TX: %s", s_radio.tx ? "ON" : "OFF");
        }
    } else if (strcmp(cmd, "drive") == 0 && args) {
        int rx, pwr;
        if (sscanf(args, "%d,%d", &rx, &pwr) == 2) {
            s_radio.drive = pwr;
        }
    } else if (strcmp(cmd, "mute") == 0 && args) {
        s_radio.mute = (strcmp(args, "true") == 0);
    } else if (strcmp(cmd, "rx_filter_band") == 0 && args) {
        int rx, low, high;
        if (sscanf(args, "%d,%d,%d", &rx, &low, &high) == 3) {
            if (rx == 0) {
                s_radio.filter_low = low;
                s_radio.filter_high = high;
            }
        }
    } else if (strcmp(cmd, "start") == 0) {
        s_radio.power_on = true;
    } else if (strcmp(cmd, "stop") == 0) {
        s_radio.power_on = false;
    } else if (strcmp(cmd, "ready") == 0) {
        set_state(TCI_STATE_READY);
        ESP_LOGI(TAG, "Thetis ready! VFO-A=%ld Hz, Mode=%s",
                 s_radio.vfo_a_freq, s_radio.mode);
    }

    // Forward to user callback
    if (s_config.notify_cb) {
        s_config.notify_cb(cmd, args);
    }
}

static void process_tci_message(const char *msg, int len)
{
    // Messages are "command:args;" or "command;"
    // The ';' terminator may or may not be present
    char buf[TCI_MAX_MSG_LEN];
    int copy_len = (len < (int)sizeof(buf) - 1) ? len : (int)sizeof(buf) - 1;
    memcpy(buf, msg, copy_len);
    buf[copy_len] = '\0';

    // Strip trailing ';'
    if (copy_len > 0 && buf[copy_len - 1] == ';') {
        buf[copy_len - 1] = '\0';
    }

    // Split on ':'
    char *colon = strchr(buf, ':');
    if (colon) {
        *colon = '\0';
        parse_tci_notification(buf, colon + 1);
    } else {
        parse_tci_notification(buf, NULL);
    }
}

// Process accumulated text data - split on ';' boundaries
static void process_incoming_text(const char *data, int data_len)
{
    for (int i = 0; i < data_len; i++) {
        if (data[i] == ';') {
            // End of TCI message
            if (s_msg_len > 0) {
                s_msg_buf[s_msg_len] = '\0';
                process_tci_message(s_msg_buf, s_msg_len);
                s_msg_len = 0;
            }
        } else if (data[i] != '\r' && data[i] != '\n') {
            if (s_msg_len < (int)sizeof(s_msg_buf) - 1) {
                s_msg_buf[s_msg_len++] = data[i];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// TCP connection
// ---------------------------------------------------------------------------

static esp_err_t tcp_connect(void)
{
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(s_config.port);

    if (inet_aton(s_config.host, &dest.sin_addr) == 0) {
        // Try DNS resolution
        struct hostent *he = gethostbyname(s_config.host);
        if (!he) {
            ESP_LOGE(TAG, "DNS resolution failed for %s", s_config.host);
            return ESP_FAIL;
        }
        memcpy(&dest.sin_addr, he->h_addr, he->h_length);
    }

    s_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed: %d", errno);
        return ESP_FAIL;
    }

    // Set connect timeout
    struct timeval tv;
    tv.tv_sec = CONNECT_TIMEOUT_MS / 1000;
    tv.tv_usec = (CONNECT_TIMEOUT_MS % 1000) * 1000;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Connecting to %s:%d...", s_config.host, s_config.port);
    set_state(TCI_STATE_CONNECTING);

    if (connect(s_sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "TCP connect failed: %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    // Set recv timeout for the WebSocket phase (longer)
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "TCP connected to %s:%d", s_config.host, s_config.port);
    return ESP_OK;
}

static void tcp_disconnect(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    set_state(TCI_STATE_DISCONNECTED);
}

// ---------------------------------------------------------------------------
// Main TCI client task
// ---------------------------------------------------------------------------

static void tci_client_task(void *arg)
{
    ESP_LOGI(TAG, "TCI client task started (target: %s:%d)",
             s_config.host, s_config.port);

    char frame_buf[TCI_RX_BUF_SIZE];

    while (!s_stop_requested) {
        // Attempt connection
        if (tcp_connect() != ESP_OK) {
            set_state(TCI_STATE_ERROR);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        // WebSocket upgrade
        set_state(TCI_STATE_WEBSOCKET_UPGRADE);
        if (do_ws_handshake() != ESP_OK) {
            tcp_disconnect();
            set_state(TCI_STATE_ERROR);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        set_state(TCI_STATE_CONNECTED);
        s_msg_len = 0;
        memset(&s_radio, 0, sizeof(s_radio));

        // Main receive loop
        while (!s_stop_requested) {
            uint8_t opcode;
            int len = ws_recv_frame(frame_buf, sizeof(frame_buf) - 1, &opcode);

            if (len < 0) {
                // Connection lost or error
                ESP_LOGW(TAG, "WS receive failed, reconnecting...");
                break;
            }

            if (len == 0) {
                // Control frame (ping/pong) handled internally
                continue;
            }

            if (opcode == WS_OP_TEXT) {
                frame_buf[len] = '\0';
                ESP_LOGD(TAG, "RX: %.*s", len, frame_buf);
                process_incoming_text(frame_buf, len);
            }
        }

        tcp_disconnect();

        if (!s_stop_requested) {
            ESP_LOGI(TAG, "Reconnecting in %d ms...", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
        }
    }

    ESP_LOGI(TAG, "TCI client task stopped");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t tci_client_init(const tci_client_config_t *config)
{
    if (!config || config->host[0] == '\0') {
        ESP_LOGE(TAG, "Invalid config: host is required");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(tci_client_config_t));
    if (s_config.port == 0) {
        s_config.port = 50001;
    }

    s_stop_requested = false;
    s_send_mutex = xSemaphoreCreateMutex();

    BaseType_t ret = xTaskCreatePinnedToCore(
        tci_client_task, "tci_client", 6144, NULL, 3, &s_task_handle, 1);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCI task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TCI client initialized (target: %s:%d)", s_config.host, s_config.port);
    return ESP_OK;
}

void tci_client_stop(void)
{
    s_stop_requested = true;

    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
    }

    if (s_task_handle) {
        // Wait for task to finish
        for (int i = 0; i < 50 && s_task_handle != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    if (s_send_mutex) {
        vSemaphoreDelete(s_send_mutex);
        s_send_mutex = NULL;
    }

    ESP_LOGI(TAG, "TCI client stopped");
}

tci_state_t tci_client_get_state(void)
{
    return s_state;
}

const tci_radio_state_t *tci_client_get_radio_state(void)
{
    return &s_radio;
}

esp_err_t tci_client_send(const char *cmd)
{
    if (s_state < TCI_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    int len = strlen(cmd);
    char buf[TCI_MAX_MSG_LEN];

    // Append ';' if missing
    if (len > 0 && cmd[len - 1] != ';') {
        snprintf(buf, sizeof(buf), "%s;", cmd);
        len++;
    } else {
        strncpy(buf, cmd, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }

    ESP_LOGD(TAG, "TX: %s", buf);
    return ws_send_text(buf, strlen(buf));
}

esp_err_t tci_client_set_vfo(int rx, int chan, long freq_hz)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "vfo:%d,%d,%ld;", rx, chan, freq_hz);
    return tci_client_send(cmd);
}

esp_err_t tci_client_set_mode(int rx, const char *mode)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "modulation:%d,%s;", rx, mode);
    return tci_client_send(cmd);
}

esp_err_t tci_client_set_ptt(int rx, bool tx)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "trx:%d,%s;", rx, tx ? "true" : "false");
    return tci_client_send(cmd);
}

esp_err_t tci_client_set_drive(int rx, int power)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "drive:%d,%d;", rx, power);
    return tci_client_send(cmd);
}

esp_err_t tci_client_set_tune(int rx, bool tune)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "tune:%d,%s;", rx, tune ? "true" : "false");
    return tci_client_send(cmd);
}

esp_err_t tci_client_set_mute(bool mute)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "mute:%s;", mute ? "true" : "false");
    return tci_client_send(cmd);
}

esp_err_t tci_client_set_split(int rx, bool split)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "split_enable:%d,%s;", rx, split ? "true" : "false");
    return tci_client_send(cmd);
}
