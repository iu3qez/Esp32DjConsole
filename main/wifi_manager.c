#include "wifi_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5

#define AP_SSID            "DJConsole-Setup"
#define AP_PASS            "djconsole"
#define AP_MAX_CONN        4

#define NVS_NAMESPACE      "wifi"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static bool s_connected = false;
static bool s_ap_mode = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_count < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying connection (%d/%d)", s_retry_count, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t err1 = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &ssid_len);
    esp_err_t err2 = nvs_get_str(nvs, NVS_KEY_PASS, pass, &pass_len);
    nvs_close(nvs);

    return (err1 == ESP_OK && err2 == ESP_OK && strlen(ssid) > 0);
}

static esp_err_t start_sta(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Starting STA mode, SSID: %s", ssid);

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "STA connection failed after %d retries", MAX_RETRY);
    return ESP_FAIL;
}

// ----- Captive portal DNS server -----
// Responds to all DNS queries with the AP's own IP (192.168.4.1).
// This triggers captive portal detection on phones/laptops.

static void dns_hijack_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive portal DNS started");

    uint8_t buf[512];
    struct sockaddr_in client;
    socklen_t client_len;

    while (1) {
        client_len = sizeof(client);
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                          (struct sockaddr *)&client, &client_len);
        if (len < 12) continue;  // Too short for DNS header

        // Build minimal DNS response: copy query, set response flags, add A record
        // DNS header: ID(2) Flags(2) QDCOUNT(2) ANCOUNT(2) NSCOUNT(2) ARCOUNT(2)
        buf[2] = 0x81;  // QR=1 (response), AA=1 (authoritative)
        buf[3] = 0x80;  // RA=1
        buf[6] = 0x00;  // ANCOUNT = 1
        buf[7] = 0x01;

        // Find end of question section (skip QNAME + QTYPE + QCLASS)
        int pos = 12;
        while (pos < len && buf[pos] != 0) {
            pos += buf[pos] + 1;  // Skip label
        }
        pos += 5;  // Skip null terminator + QTYPE(2) + QCLASS(2)

        if (pos + 16 > (int)sizeof(buf)) continue;

        // Append answer: pointer to name + TYPE A + CLASS IN + TTL + RDLENGTH + IP
        buf[pos++] = 0xC0;  // Name pointer to offset 12 (question name)
        buf[pos++] = 0x0C;
        buf[pos++] = 0x00;  // TYPE A
        buf[pos++] = 0x01;
        buf[pos++] = 0x00;  // CLASS IN
        buf[pos++] = 0x01;
        buf[pos++] = 0x00;  // TTL = 60 seconds
        buf[pos++] = 0x00;
        buf[pos++] = 0x00;
        buf[pos++] = 0x3C;
        buf[pos++] = 0x00;  // RDLENGTH = 4
        buf[pos++] = 0x04;
        buf[pos++] = 192;   // 192.168.4.1 (default AP IP)
        buf[pos++] = 168;
        buf[pos++] = 4;
        buf[pos++] = 1;

        sendto(sock, buf, pos, 0, (struct sockaddr *)&client, client_len);
    }
}

static esp_err_t start_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode, SSID: %s", AP_SSID);

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_mode = true;

    // Start captive portal DNS hijack
    xTaskCreate(dns_hijack_task, "dns_hijack", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "AP started. Connect to '%s' (pass: '%s') and configure WiFi via web GUI",
             AP_SSID, AP_PASS);
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    char ssid[33] = {0};
    char pass[65] = {0};

    if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        esp_err_t ret = start_sta(ssid, pass);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        // STA failed, clean up and fall through to AP
        esp_wifi_stop();
        esp_wifi_deinit();
    }

    return start_ap();
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    nvs_set_str(nvs, NVS_KEY_PASS, password);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Credentials saved. Restarting WiFi...");
    esp_restart();
    return ESP_OK;  // unreachable
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

bool wifi_manager_is_ap_mode(void)
{
    return s_ap_mode;
}
