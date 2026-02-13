#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mdns.h"

#include "wifi_manager.h"
#include "status_led.h"
#include "usb_dj_host.h"
#include "usb_debug.h"
#include "tci_client.h"
#include "cat_client.h"
#include "config_store.h"
#include "mapping_engine.h"
#include "http_server.h"

static const char *TAG = "main";

static void init_mdns(void)
{
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set("djconsole");
    mdns_instance_name_set("DJ Console Controller");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started: djconsole.local");
}

// Combined USB callback: debug logging + mapping engine dispatch
static void usb_control_cb(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value)
{
    // Debug logging first
    usb_debug_control_cb(name, control_type, control_index, old_value, new_value);

    // Dispatch to mapping engine
    mapping_engine_on_control(name, control_type, control_index, old_value, new_value);

    // Notify WebSocket clients
    http_server_notify_control(name, control_type, old_value, new_value);
}

// TCI state change callback - update LED
static void tci_state_cb(tci_state_t new_state)
{
    switch (new_state) {
    case TCI_STATE_CONNECTING:
        ESP_LOGI(TAG, "TCI: connecting...");
        break;
    case TCI_STATE_CONNECTED:
        ESP_LOGI(TAG, "TCI: WebSocket connected");
        break;
    case TCI_STATE_READY:
        ESP_LOGI(TAG, "TCI: Thetis ready");
        status_led_set(LED_CYAN);
        http_server_notify_status();
        break;
    case TCI_STATE_DISCONNECTED:
        ESP_LOGW(TAG, "TCI: disconnected");
        http_server_notify_status();
        if (usb_dj_host_is_connected()) {
            status_led_set(LED_PURPLE);
        } else {
            status_led_blink(LED_YELLOW, 500);
        }
        break;
    case TCI_STATE_ERROR:
        ESP_LOGE(TAG, "TCI: error");
        break;
    default:
        break;
    }
}

// CAT state change callback
static void cat_state_cb(cat_state_t new_state)
{
    switch (new_state) {
    case CAT_STATE_CONNECTED:
        ESP_LOGI(TAG, "CAT: connected");
        status_led_set(LED_CYAN);
        http_server_notify_status();
        break;
    case CAT_STATE_DISCONNECTED:
        ESP_LOGW(TAG, "CAT: disconnected");
        http_server_notify_status();
        if (usb_dj_host_is_connected()) {
            status_led_set(LED_PURPLE);
        }
        break;
    default:
        break;
    }
}

// TCI notification callback - log incoming state from Thetis
static void tci_notify_cb(const char *cmd, const char *args)
{
    ESP_LOGD(TAG, "TCI notify: %s:%s", cmd, args ? args : "");
    http_server_notify_radio();
}

// CAT response callback
static void cat_response_cb(const char *cmd, const char *value)
{
    ESP_LOGD(TAG, "CAT response: %s = %s", cmd, value);
}

// Start TCI or CAT client based on NVS config
static void start_radio_client(void)
{
    char proto[8] = "tci";
    config_get_str(CFG_KEY_PROTOCOL, proto, sizeof(proto));

    bool use_tci = (strcmp(proto, "cat") != 0);

    if (use_tci) {
        char host[64] = {0};
        uint16_t port = 50001;
        config_get_str(CFG_KEY_TCI_HOST, host, sizeof(host));
        config_get_u16(CFG_KEY_TCI_PORT, &port);

        if (host[0] == '\0') {
            ESP_LOGI(TAG, "TCI: no host configured, skipping");
            return;
        }

        tci_client_config_t cfg = {
            .port = port,
            .state_cb = tci_state_cb,
            .notify_cb = tci_notify_cb,
        };
        strncpy(cfg.host, host, sizeof(cfg.host) - 1);

        if (tci_client_init(&cfg) == ESP_OK) {
            ESP_LOGI(TAG, "TCI client started (%s:%d)", host, port);
        }
    } else {
        char host[64] = {0};
        uint16_t port = 31001;
        config_get_str(CFG_KEY_CAT_HOST, host, sizeof(host));
        config_get_u16(CFG_KEY_CAT_PORT, &port);

        if (host[0] == '\0') {
            ESP_LOGI(TAG, "CAT: no host configured, skipping");
            return;
        }

        cat_client_config_t cfg = {
            .port = port,
            .state_cb = cat_state_cb,
            .response_cb = cat_response_cb,
        };
        strncpy(cfg.host, host, sizeof(cfg.host) - 1);

        if (cat_client_init(&cfg) == ESP_OK) {
            ESP_LOGI(TAG, "CAT client started (%s:%d)", host, port);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 DJ Console ===");

    // Initialize RGB status LED
    status_led_init();
    status_led_blink(LED_YELLOW, 500);  // Blinking yellow = starting up

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi (STA with fallback to AP)
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        status_led_set(LED_RED);  // Red = error
    } else if (wifi_manager_is_connected()) {
        status_led_set(LED_GREEN);  // Green = STA connected
    } else {
        status_led_blink(LED_BLUE, 1000);  // Blinking blue = AP mode, waiting for setup
    }

    // Start mDNS
    init_mdns();

    // Start USB host driver with combined callback (debug + mapping)
    usb_debug_set_level(1);
    ret = usb_dj_host_init(usb_control_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB host init failed: %s", esp_err_to_name(ret));
        status_led_set(LED_RED);
    } else {
        usb_dj_host_set_raw_callback(usb_debug_raw_state_cb);
        ESP_LOGI(TAG, "USB host started (debug level %d)", usb_debug_get_level());
    }

    // Initialize mapping engine (loads from NVS or uses defaults)
    mapping_engine_init();

    // Start TCI or CAT client if WiFi is connected and host is configured
    if (wifi_manager_is_connected()) {
        start_radio_client();
    }

    // Start HTTP server (REST API + WebSocket + static files)
    ret = http_server_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server init failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "System ready. Free heap: %lu bytes", esp_get_free_heap_size());
}
