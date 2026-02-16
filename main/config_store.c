#include "config_store.h"

#include <stdlib.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "config";

esp_err_t config_get_str(const char *key, char *out, size_t max_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_get_str(nvs, key, out, &max_len);
    nvs_close(nvs);
    return err;
}

esp_err_t config_set_str(const char *key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs, key, value);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Set %s = %s", key, value);
    }
    return err;
}

esp_err_t config_get_u16(const char *key, uint16_t *out)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_get_u16(nvs, key, out);
    nvs_close(nvs);
    return err;
}

esp_err_t config_set_u16(const char *key, uint16_t value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_u16(nvs, key, value);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t config_get_u8(const char *key, uint8_t *out)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_get_u8(nvs, key, out);
    nvs_close(nvs);
    return err;
}

esp_err_t config_set_u8(const char *key, uint8_t value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(nvs, key, value);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

