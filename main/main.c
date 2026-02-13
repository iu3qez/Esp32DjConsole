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

static const char *TAG = "main";

static void init_mdns(void)
{
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set("djconsole");
    mdns_instance_name_set("DJ Console Controller");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started: djconsole.local");
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
        break;
    case TCI_STATE_DISCONNECTED:
        ESP_LOGW(TAG, "TCI: disconnected");
        // Restore LED based on USB state
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

// TCI notification callback - log incoming state from Thetis
static void tci_notify_cb(const char *cmd, const char *args)
{
    ESP_LOGI(TAG, "TCI notify: %s:%s", cmd, args ? args : "");
}

// Try to start TCI client from NVS-stored config
static void start_tci_if_configured(void)
{
    nvs_handle_t nvs;
    if (nvs_open("djconfig", NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "TCI: no config stored yet, skipping");
        return;
    }

    char host[64] = {0};
    size_t host_len = sizeof(host);
    uint16_t port = 50001;

    esp_err_t err = nvs_get_str(nvs, "tci_host", host, &host_len);
    nvs_get_u16(nvs, "tci_port", &port);
    nvs_close(nvs);

    if (err != ESP_OK || host[0] == '\0') {
        ESP_LOGI(TAG, "TCI: no host configured, skipping");
        return;
    }

    tci_client_config_t tci_cfg = {
        .port = port,
        .state_cb = tci_state_cb,
        .notify_cb = tci_notify_cb,
    };
    strncpy(tci_cfg.host, host, sizeof(tci_cfg.host) - 1);

    esp_err_t ret = tci_client_init(&tci_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCI client init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "TCI client started (target: %s:%d)", host, port);
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

    // Start USB host driver with debug callbacks
    usb_debug_set_level(1);  // Default: log control changes
    ret = usb_dj_host_init(usb_debug_control_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB host init failed: %s", esp_err_to_name(ret));
        status_led_set(LED_RED);
    } else {
        usb_dj_host_set_raw_callback(usb_debug_raw_state_cb);
        ESP_LOGI(TAG, "USB host started (debug level %d)", usb_debug_get_level());
    }

    // Start TCI client if WiFi is connected and host is configured
    if (wifi_manager_is_connected()) {
        start_tci_if_configured();
    }

    ESP_LOGI(TAG, "System ready. Free heap: %lu bytes", esp_get_free_heap_size());

    // TODO Phase 6: Start HTTP server
}
