#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"

#include "wifi_manager.h"
#include "status_led.h"
#include "usb_dj_host.h"
#include "usb_debug.h"

static const char *TAG = "main";

static void init_mdns(void)
{
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set("djconsole");
    mdns_instance_name_set("DJ Console Controller");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started: djconsole.local");
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

    ESP_LOGI(TAG, "System ready. Free heap: %lu bytes", esp_get_free_heap_size());

    // TODO Phase 3: Start TCI client
    //   -> LED_CYAN when TCI connected
    // TODO Phase 6: Start HTTP server
}
