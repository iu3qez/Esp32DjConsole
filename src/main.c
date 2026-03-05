#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tusb.h"
#include "host/hcd.h"
#include "esp_private/usb_phy.h"
#include "soc/lp_system_struct.h"

static const char *TAG = "midi_bridge";

static usb_phy_handle_t fs_phy_hdl;
static usb_phy_handle_t hs_phy_hdl;

//--------------------------------------------------------------------+
// PHY Initialization for ESP32-P4 dual USB
//--------------------------------------------------------------------+
static bool usb_phy_dual_init(void) {
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1;
    LP_SYS.usb_ctrl.sw_usb_phy_sel = 1;
    ESP_LOGI(TAG, "USB FS PHY mux swapped: OTG_FS->PHY0, USJ->PHY1");

    usb_phy_config_t fs_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_INT,
        .otg_mode   = USB_OTG_MODE_DEVICE,
        .otg_speed  = USB_PHY_SPEED_FULL,
    };
    esp_err_t ret = usb_new_phy(&fs_conf, &fs_phy_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FS PHY init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "FS PHY initialized — device mode");

    usb_phy_config_t hs_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_UTMI,
        .otg_mode   = USB_OTG_MODE_HOST,
        .otg_speed  = USB_PHY_SPEED_HIGH,
    };
    ret = usb_new_phy(&hs_conf, &hs_phy_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HS PHY init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "HS PHY initialized — host mode");

    return true;
}

//--------------------------------------------------------------------+
// TinyUSB tasks (FreeRTOS)
//--------------------------------------------------------------------+
static void tusb_device_task(void *arg) {
    (void) arg;
    ESP_LOGI(TAG, "TinyUSB device task started");
    while (1) {
        tud_task_ext(1, false);
    }
}

static void tusb_host_task(void *arg) {
    (void) arg;
    ESP_LOGI(TAG, "TinyUSB host task started");
    while (1) {
        tuh_task_ext(1, false);
    }
}

//--------------------------------------------------------------------+
// USB init task
//--------------------------------------------------------------------+
static void usb_init_task(void *arg) {
    (void) arg;

    if (!usb_phy_dual_init()) {
        ESP_LOGE(TAG, "USB PHY init failed, aborting");
        vTaskDelete(NULL);
        return;
    }

    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    if (!tud_rhport_init(0, &dev_init)) {
        ESP_LOGE(TAG, "TinyUSB device init failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "TinyUSB device initialized on rhport 0 (FS)");

    tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_HIGH,
    };
    if (!tuh_rhport_init(1, &host_init)) {
        ESP_LOGE(TAG, "TinyUSB host init failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "TinyUSB host initialized on rhport 1 (HS)");

    xTaskCreatePinnedToCore(tusb_device_task, "usbd", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(tusb_host_task, "usbh", 4096, NULL, 5, NULL, 1);

    // Trigger enumeration if device already connected
    vTaskDelay(pdMS_TO_TICKS(500));
    if (hcd_port_connect_status(1)) {
        hcd_event_device_attach(1, false);
    }

    vTaskDelete(NULL);
}

//--------------------------------------------------------------------+
// Device callbacks (stubs for now)
//--------------------------------------------------------------------+
void tud_mount_cb(void) {
    ESP_LOGI(TAG, "MIDI device mounted");
}

void tud_umount_cb(void) {
    ESP_LOGI(TAG, "MIDI device unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
}

void tud_resume_cb(void) {
}

//--------------------------------------------------------------------+
// app_main
//--------------------------------------------------------------------+
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32-P4 MIDI Bridge ===");
    xTaskCreate(usb_init_task, "usb_init", 16384, NULL, 5, NULL);
}
