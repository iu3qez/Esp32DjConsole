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

// Combined USB callback: debug logging + mapping engine dispatch + WS notify
static void usb_control_cb(
    const char *name,
    dj_control_type_t control_type,
    uint8_t control_index,
    uint8_t old_value,
    uint8_t new_value)
{
    usb_debug_control_cb(name, control_type, control_index, old_value, new_value);
    mapping_engine_on_control(name, control_type, control_index, old_value, new_value);
    http_server_notify_control(name, control_type, old_value, new_value);
}

// CAT state change callback
static void cat_state_cb(cat_state_t new_state)
{
    switch (new_state) {
    case CAT_STATE_CONNECTED:
        ESP_LOGI(TAG, "CAT: connected");
        status_led_set(LED_CYAN);
        http_server_notify_status();
        mapping_engine_request_sync();  // Query ZZFA, ZZFB, ZZAC from Thetis
        break;
    case CAT_STATE_DISCONNECTED:
        ESP_LOGW(TAG, "CAT: disconnected");
        http_server_notify_status();
        if (usb_dj_host_is_connected()) {
            status_led_set(LED_PURPLE);
        }
        break;
    case CAT_STATE_CONNECTING:
        ESP_LOGI(TAG, "CAT: connecting...");
        break;
    case CAT_STATE_ERROR:
        ESP_LOGE(TAG, "CAT: error");
        break;
    }
}

// CAT response callback — forward to mapping engine for VFO/step sync
static void cat_response_cb(const char *cmd, const char *value)
{
    ESP_LOGD(TAG, "CAT response: %s = %s", cmd, value);
    mapping_engine_on_cat_response(cmd, value);
}

// Start CAT client from NVS config
static void start_cat_client(void)
{
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

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 DJ Console ===");

    // Initialize RGB status LED
    status_led_init();
    status_led_blink(LED_YELLOW, 500);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        status_led_set(LED_RED);
    } else if (wifi_manager_is_connected()) {
        status_led_set(LED_GREEN);
    } else {
        status_led_blink(LED_BLUE, 1000);
    }

    // Start mDNS
    init_mdns();

    // Start USB host driver
    usb_debug_set_level(1);
    ret = usb_dj_host_init(usb_control_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB host init failed: %s", esp_err_to_name(ret));
        status_led_set(LED_RED);
    } else {
        usb_dj_host_set_raw_callback(usb_debug_raw_state_cb);
        ESP_LOGI(TAG, "USB host started (debug level %d)", usb_debug_get_level());
    }

    // Initialize mapping engine (loads from SPIFFS or uses defaults)
    mapping_engine_init();

    // Start CAT client if WiFi is connected and host configured
    if (wifi_manager_is_connected()) {
        start_cat_client();
    }

    // Start HTTP server
    ret = http_server_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server init failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "System ready. Free heap: %lu bytes", esp_get_free_heap_size());
}
