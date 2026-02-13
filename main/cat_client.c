#include "cat_client.h"
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

static const char *TAG = "cat";

#define CAT_MAX_CMD_LEN    64
#define CAT_RX_BUF_SIZE    512
#define RECONNECT_DELAY_MS 3000
#define CONNECT_TIMEOUT_MS 5000
#define RECV_TIMEOUT_MS    30000

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static cat_client_config_t s_config;
static cat_state_t         s_state = CAT_STATE_DISCONNECTED;
static int                 s_sock = -1;
static TaskHandle_t        s_task_handle = NULL;
static SemaphoreHandle_t   s_send_mutex = NULL;
static bool                s_stop_requested = false;

// Message accumulator (CAT messages end with ';')
static char s_msg_buf[CAT_RX_BUF_SIZE];
static int  s_msg_len = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void set_state(cat_state_t new_state)
{
    if (s_state != new_state) {
        ESP_LOGI(TAG, "State: %d -> %d", s_state, new_state);
        s_state = new_state;
        if (s_config.state_cb) {
            s_config.state_cb(new_state);
        }
    }
}

// ---------------------------------------------------------------------------
// CAT response parsing
// ---------------------------------------------------------------------------

static void process_cat_message(const char *msg, int len)
{
    // Messages are "ZZXXvalue;" or "CMDvalue;" or "?;" or "#...#;"
    // We receive them without the trailing ';' (stripped by caller)

    if (len == 0) return;

    // Skip welcome/info messages starting with '#'
    if (msg[0] == '#') {
        ESP_LOGI(TAG, "Server: %s", msg);
        return;
    }

    // Error response
    if (msg[0] == '?' || msg[0] == 'E' || msg[0] == 'O') {
        ESP_LOGW(TAG, "CAT error: %s", msg);
        return;
    }

    // ZZ extended commands: prefix is 4 chars (ZZXX), value is the rest
    if (len >= 4 && msg[0] == 'Z' && msg[1] == 'Z') {
        char cmd[5];
        memcpy(cmd, msg, 4);
        cmd[4] = '\0';

        const char *value = (len > 4) ? msg + 4 : "";

        ESP_LOGD(TAG, "Response: %s = %s", cmd, value);

        if (s_config.response_cb) {
            s_config.response_cb(cmd, value);
        }
        return;
    }

    // Standard Kenwood: prefix is 2 chars
    if (len >= 2) {
        char cmd[3];
        memcpy(cmd, msg, 2);
        cmd[2] = '\0';

        const char *value = (len > 2) ? msg + 2 : "";

        ESP_LOGD(TAG, "Response: %s = %s", cmd, value);

        if (s_config.response_cb) {
            s_config.response_cb(cmd, value);
        }
    }
}

// Process incoming data, splitting on ';' boundaries
static void process_incoming_data(const char *data, int data_len)
{
    for (int i = 0; i < data_len; i++) {
        if (data[i] == ';') {
            if (s_msg_len > 0) {
                s_msg_buf[s_msg_len] = '\0';
                process_cat_message(s_msg_buf, s_msg_len);
                s_msg_len = 0;
            }
        } else if (data[i] != '\r' && data[i] != '\n') {
            if (s_msg_len < (int)sizeof(s_msg_buf) - 1) {
                s_msg_buf[s_msg_len++] = data[i];
            } else {
                // Buffer overflow - discard
                s_msg_len = 0;
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

    // Connect timeout
    struct timeval tv;
    tv.tv_sec = CONNECT_TIMEOUT_MS / 1000;
    tv.tv_usec = (CONNECT_TIMEOUT_MS % 1000) * 1000;
    setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Connecting to %s:%d...", s_config.host, s_config.port);
    set_state(CAT_STATE_CONNECTING);

    if (connect(s_sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "TCP connect failed: %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    // Recv timeout
    tv.tv_sec = RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = 0;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "CAT TCP connected to %s:%d", s_config.host, s_config.port);
    set_state(CAT_STATE_CONNECTED);
    return ESP_OK;
}

static void tcp_disconnect(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    set_state(CAT_STATE_DISCONNECTED);
}

// ---------------------------------------------------------------------------
// Main CAT client task
// ---------------------------------------------------------------------------

static void cat_client_task(void *arg)
{
    ESP_LOGI(TAG, "CAT client task started (target: %s:%d)",
             s_config.host, s_config.port);

    char rx_buf[CAT_RX_BUF_SIZE];

    while (!s_stop_requested) {
        if (tcp_connect() != ESP_OK) {
            set_state(CAT_STATE_ERROR);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        s_msg_len = 0;

        // Receive loop
        while (!s_stop_requested) {
            int n = recv(s_sock, rx_buf, sizeof(rx_buf) - 1, 0);

            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Recv timeout - send a keepalive query
                    // Thetis disconnects after 30s of inactivity
                    cat_client_query_smeter();
                    continue;
                }
                ESP_LOGW(TAG, "Recv error: %d, reconnecting...", errno);
                break;
            }

            if (n == 0) {
                ESP_LOGW(TAG, "Connection closed by server");
                break;
            }

            rx_buf[n] = '\0';
            ESP_LOGD(TAG, "RX (%d bytes): %s", n, rx_buf);
            process_incoming_data(rx_buf, n);
        }

        tcp_disconnect();

        if (!s_stop_requested) {
            ESP_LOGI(TAG, "Reconnecting in %d ms...", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
        }
    }

    ESP_LOGI(TAG, "CAT client task stopped");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t cat_client_init(const cat_client_config_t *config)
{
    if (!config || config->host[0] == '\0') {
        ESP_LOGE(TAG, "Invalid config: host is required");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(cat_client_config_t));
    if (s_config.port == 0) {
        s_config.port = 31001;  // TCPIPcatServer.cs DEFAULT_PORT
    }

    s_stop_requested = false;
    s_send_mutex = xSemaphoreCreateMutex();

    BaseType_t ret = xTaskCreatePinnedToCore(
        cat_client_task, "cat_client", 4096, NULL, 3, &s_task_handle, 1);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CAT task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CAT client initialized (target: %s:%d)", s_config.host, s_config.port);
    return ESP_OK;
}

void cat_client_stop(void)
{
    s_stop_requested = true;

    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
    }

    if (s_task_handle) {
        for (int i = 0; i < 50 && s_task_handle != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    if (s_send_mutex) {
        vSemaphoreDelete(s_send_mutex);
        s_send_mutex = NULL;
    }

    ESP_LOGI(TAG, "CAT client stopped");
}

cat_state_t cat_client_get_state(void)
{
    return s_state;
}

esp_err_t cat_client_send(const char *cmd)
{
    if (s_state < CAT_STATE_CONNECTED || s_sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char buf[CAT_MAX_CMD_LEN];
    int len = strlen(cmd);

    // Append ';' if missing
    if (len > 0 && cmd[len - 1] != ';') {
        snprintf(buf, sizeof(buf), "%s;", cmd);
    } else {
        strncpy(buf, cmd, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }

    len = strlen(buf);
    ESP_LOGD(TAG, "TX: %s", buf);

    int sent = 0;
    while (sent < len) {
        int n = send(s_sock, buf + sent, len - sent, 0);
        if (n < 0) {
            xSemaphoreGive(s_send_mutex);
            return ESP_FAIL;
        }
        sent += n;
    }

    xSemaphoreGive(s_send_mutex);
    return ESP_OK;
}

// Frequency: 11 digits, zero-padded (e.g., 14074000 -> "00014074000")
esp_err_t cat_client_set_vfo_a(long freq_hz)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "ZZFA%011ld;", freq_hz);
    return cat_client_send(cmd);
}

esp_err_t cat_client_set_vfo_b(long freq_hz)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "ZZFB%011ld;", freq_hz);
    return cat_client_send(cmd);
}

// Mode: 2 digits zero-padded
esp_err_t cat_client_set_mode(cat_mode_t mode)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ZZMD%02d;", (int)mode);
    return cat_client_send(cmd);
}

// Volume: 3 digits, 0-100
esp_err_t cat_client_set_volume(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ZZAG%03d;", level);
    return cat_client_send(cmd);
}

// Drive power: 3 digits, 0-100
esp_err_t cat_client_set_drive(int power)
{
    if (power < 0) power = 0;
    if (power > 100) power = 100;
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ZZPC%03d;", power);
    return cat_client_send(cmd);
}

// PTT: 1 digit
esp_err_t cat_client_set_ptt(bool tx)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ZZTX%d;", tx ? 1 : 0);
    return cat_client_send(cmd);
}

// Tune: 1 digit
esp_err_t cat_client_set_tune(bool tune)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ZZTU%d;", tune ? 1 : 0);
    return cat_client_send(cmd);
}

// Mute RX1: 1 digit
esp_err_t cat_client_set_mute(bool mute)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ZZMA%d;", mute ? 1 : 0);
    return cat_client_send(cmd);
}

// Split: 1 digit
esp_err_t cat_client_set_split(bool split)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ZZSP%d;", split ? 1 : 0);
    return cat_client_send(cmd);
}

// Query S-meter (response via callback: "ZZSM0xxx;")
esp_err_t cat_client_query_smeter(void)
{
    return cat_client_send("ZZSM0;");
}

esp_err_t cat_client_query_vfo_a(void)
{
    return cat_client_send("ZZFA;");
}

esp_err_t cat_client_query_vfo_b(void)
{
    return cat_client_send("ZZFB;");
}

esp_err_t cat_client_query_mode(void)
{
    return cat_client_send("ZZMD;");
}
