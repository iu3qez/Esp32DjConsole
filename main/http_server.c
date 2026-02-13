#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "cJSON.h"

#include "http_server.h"
#include "config_store.h"
#include "mapping_engine.h"
#include "tci_client.h"
#include "cat_client.h"
#include "usb_dj_host.h"
#include "usb_debug.h"
#include "wifi_manager.h"

static const char *TAG = "http_srv";

static httpd_handle_t s_server = NULL;

// ----- WebSocket client tracking -----

#define MAX_WS_CLIENTS 4

static int s_ws_fds[MAX_WS_CLIENTS];
static int s_ws_count = 0;

static void ws_add_client(int fd)
{
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) return;
    }
    if (s_ws_count < MAX_WS_CLIENTS) {
        s_ws_fds[s_ws_count++] = fd;
        ESP_LOGI(TAG, "WS client connected (fd=%d, total=%d)", fd, s_ws_count);
    }
}

static void ws_remove_client(int fd)
{
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = s_ws_fds[--s_ws_count];
            ESP_LOGI(TAG, "WS client disconnected (fd=%d, total=%d)", fd, s_ws_count);
            return;
        }
    }
}

void http_server_ws_broadcast(const char *json)
{
    if (!s_server || s_ws_count == 0) return;

    httpd_ws_frame_t pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    for (int i = 0; i < s_ws_count; ) {
        esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &pkt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WS send failed fd=%d: %s, removing", s_ws_fds[i], esp_err_to_name(ret));
            ws_remove_client(s_ws_fds[i]);
            // don't increment i, the swap put a new fd at position i
        } else {
            i++;
        }
    }
}

// ----- Notification helpers -----

void http_server_notify_control(
    const char *name,
    dj_control_type_t control_type,
    uint8_t old_value,
    uint8_t new_value)
{
    if (s_ws_count == 0) return;

    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"control\",\"name\":\"%s\",\"ctrl\":%d,\"old\":%d,\"new\":%d}",
        name, (int)control_type, old_value, new_value);
    http_server_ws_broadcast(buf);
}

void http_server_notify_radio(void)
{
    if (s_ws_count == 0) return;

    const tci_radio_state_t *rs = tci_client_get_radio_state();
    if (!rs) return;

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"radio\",\"vfo_a\":%ld,\"vfo_b\":%ld,\"mode\":\"%s\","
        "\"drive\":%d,\"tx\":%s,\"mute\":%s,\"filter_low\":%d,\"filter_high\":%d}",
        rs->vfo_a_freq, rs->vfo_b_freq, rs->mode,
        rs->drive, rs->tx ? "true" : "false", rs->mute ? "true" : "false",
        rs->filter_low, rs->filter_high);
    http_server_ws_broadcast(buf);
}

void http_server_notify_status(void)
{
    if (s_ws_count == 0) return;

    const char *tci_str = "disconnected";
    tci_state_t ts = tci_client_get_state();
    switch (ts) {
    case TCI_STATE_CONNECTING:        tci_str = "connecting"; break;
    case TCI_STATE_WEBSOCKET_UPGRADE: tci_str = "upgrading"; break;
    case TCI_STATE_CONNECTED:         tci_str = "connected"; break;
    case TCI_STATE_READY:             tci_str = "ready"; break;
    case TCI_STATE_ERROR:             tci_str = "error"; break;
    default: break;
    }

    const char *cat_str = "disconnected";
    cat_state_t cs = cat_client_get_state();
    switch (cs) {
    case CAT_STATE_CONNECTING: cat_str = "connecting"; break;
    case CAT_STATE_CONNECTED:  cat_str = "connected"; break;
    case CAT_STATE_ERROR:      cat_str = "error"; break;
    default: break;
    }

    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"status\",\"usb\":%s,\"tci\":\"%s\",\"cat\":\"%s\",\"heap\":%lu}",
        usb_dj_host_is_connected() ? "true" : "false",
        tci_str, cat_str,
        esp_get_free_heap_size());
    http_server_ws_broadcast(buf);
}

// ----- WebSocket handler -----

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // WebSocket handshake - register this client
        ws_add_client(httpd_req_to_sockfd(req));
        // Send initial status
        http_server_notify_status();
        http_server_notify_radio();
        return ESP_OK;
    }

    // Receive a frame (client message)
    httpd_ws_frame_t pkt = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) return ret;

    if (pkt.len > 0) {
        uint8_t *buf = malloc(pkt.len + 1);
        if (!buf) return ESP_ERR_NO_MEM;
        pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
        if (ret == ESP_OK) {
            buf[pkt.len] = '\0';
            ESP_LOGD(TAG, "WS recv: %s", (char *)buf);
            // Could handle client commands here (e.g., ping)
        }
        free(buf);
    }
    return ret;
}

// ----- REST API: GET /api/status -----

static esp_err_t api_status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    // USB
    cJSON_AddBoolToObject(root, "usb_connected", usb_dj_host_is_connected());
    cJSON_AddNumberToObject(root, "usb_updates", usb_debug_get_update_count());
    cJSON_AddNumberToObject(root, "usb_changes", usb_debug_get_change_count());

    // TCI state
    const char *tci_states[] = {"disconnected","connecting","upgrading","connected","ready","error"};
    tci_state_t ts = tci_client_get_state();
    cJSON_AddStringToObject(root, "tci_state",
        (ts >= 0 && ts <= TCI_STATE_ERROR) ? tci_states[ts] : "unknown");

    // CAT state
    const char *cat_states[] = {"disconnected","connecting","connected","error"};
    cat_state_t cs = cat_client_get_state();
    cJSON_AddStringToObject(root, "cat_state",
        (cs >= 0 && cs <= CAT_STATE_ERROR) ? cat_states[cs] : "unknown");

    // Radio state from TCI
    const tci_radio_state_t *rs = tci_client_get_radio_state();
    if (rs) {
        cJSON *radio = cJSON_AddObjectToObject(root, "radio");
        cJSON_AddNumberToObject(radio, "vfo_a", rs->vfo_a_freq);
        cJSON_AddNumberToObject(radio, "vfo_b", rs->vfo_b_freq);
        cJSON_AddStringToObject(radio, "mode", rs->mode);
        cJSON_AddNumberToObject(radio, "drive", rs->drive);
        cJSON_AddBoolToObject(radio, "tx", rs->tx);
        cJSON_AddBoolToObject(radio, "mute", rs->mute);
        cJSON_AddNumberToObject(radio, "filter_low", rs->filter_low);
        cJSON_AddNumberToObject(radio, "filter_high", rs->filter_high);
        cJSON_AddBoolToObject(radio, "power_on", rs->power_on);
    }

    // Protocol
    char proto[8] = "tci";
    config_get_str(CFG_KEY_PROTOCOL, proto, sizeof(proto));
    cJSON_AddStringToObject(root, "protocol", proto);

    // WiFi
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_manager_is_connected());

    // System
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ----- REST API: GET /api/config -----

static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    char val[64];

    // WiFi
    if (config_get_str(CFG_KEY_WIFI_SSID, val, sizeof(val)) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_ssid", val);
    } else {
        cJSON_AddStringToObject(root, "wifi_ssid", "");
    }
    // Don't expose password, just indicate if set
    cJSON_AddBoolToObject(root, "wifi_pass_set",
        config_get_str(CFG_KEY_WIFI_PASS, val, sizeof(val)) == ESP_OK && val[0] != '\0');

    // Protocol
    char proto[8] = "tci";
    config_get_str(CFG_KEY_PROTOCOL, proto, sizeof(proto));
    cJSON_AddStringToObject(root, "protocol", proto);

    // TCI
    val[0] = '\0';
    config_get_str(CFG_KEY_TCI_HOST, val, sizeof(val));
    cJSON_AddStringToObject(root, "tci_host", val);
    uint16_t port = 50001;
    config_get_u16(CFG_KEY_TCI_PORT, &port);
    cJSON_AddNumberToObject(root, "tci_port", port);

    // CAT
    val[0] = '\0';
    config_get_str(CFG_KEY_CAT_HOST, val, sizeof(val));
    cJSON_AddStringToObject(root, "cat_host", val);
    port = 31001;
    config_get_u16(CFG_KEY_CAT_PORT, &port);
    cJSON_AddNumberToObject(root, "cat_port", port);

    // Debug level
    uint8_t dbg = 1;
    config_get_u8(CFG_KEY_DEBUG_LEVEL, &dbg);
    cJSON_AddNumberToObject(root, "debug_level", dbg);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ----- REST API: PUT /api/config -----

static esp_err_t api_config_put_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char *buf = malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool need_wifi_restart = false;

    // WiFi SSID
    cJSON *item = cJSON_GetObjectItem(root, "wifi_ssid");
    if (item && cJSON_IsString(item)) {
        config_set_str(CFG_KEY_WIFI_SSID, item->valuestring);
        need_wifi_restart = true;
    }

    // WiFi password
    item = cJSON_GetObjectItem(root, "wifi_pass");
    if (item && cJSON_IsString(item)) {
        config_set_str(CFG_KEY_WIFI_PASS, item->valuestring);
        need_wifi_restart = true;
    }

    // Protocol
    item = cJSON_GetObjectItem(root, "protocol");
    if (item && cJSON_IsString(item)) {
        if (strcmp(item->valuestring, "tci") == 0 || strcmp(item->valuestring, "cat") == 0) {
            config_set_str(CFG_KEY_PROTOCOL, item->valuestring);
            mapping_engine_set_protocol(strcmp(item->valuestring, "tci") == 0);
        }
    }

    // TCI host/port
    item = cJSON_GetObjectItem(root, "tci_host");
    if (item && cJSON_IsString(item)) {
        config_set_str(CFG_KEY_TCI_HOST, item->valuestring);
    }
    item = cJSON_GetObjectItem(root, "tci_port");
    if (item && cJSON_IsNumber(item)) {
        config_set_u16(CFG_KEY_TCI_PORT, (uint16_t)item->valueint);
    }

    // CAT host/port
    item = cJSON_GetObjectItem(root, "cat_host");
    if (item && cJSON_IsString(item)) {
        config_set_str(CFG_KEY_CAT_HOST, item->valuestring);
    }
    item = cJSON_GetObjectItem(root, "cat_port");
    if (item && cJSON_IsNumber(item)) {
        config_set_u16(CFG_KEY_CAT_PORT, (uint16_t)item->valueint);
    }

    // Debug level
    item = cJSON_GetObjectItem(root, "debug_level");
    if (item && cJSON_IsNumber(item)) {
        uint8_t lvl = (uint8_t)item->valueint;
        config_set_u8(CFG_KEY_DEBUG_LEVEL, lvl);
        usb_debug_set_level(lvl);
    }

    cJSON_Delete(root);

    // Response
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "restart_required", need_wifi_restart);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    // Apply WiFi changes
    if (need_wifi_restart) {
        char ssid[64] = {0}, pass[64] = {0};
        config_get_str(CFG_KEY_WIFI_SSID, ssid, sizeof(ssid));
        config_get_str(CFG_KEY_WIFI_PASS, pass, sizeof(pass));
        if (ssid[0] != '\0') {
            ESP_LOGI(TAG, "WiFi credentials updated, restarting WiFi...");
            wifi_manager_set_credentials(ssid, pass);
        }
    }

    return ESP_OK;
}

// ----- REST API: GET /api/mappings -----

static esp_err_t api_mappings_get_handler(httpd_req_t *req)
{
    int count = 0;
    const mapping_entry_t *table = mapping_engine_get_table(&count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "control", table[i].control_name);
        cJSON_AddNumberToObject(entry, "action", table[i].action);
        cJSON_AddNumberToObject(entry, "param_int", table[i].param_int);
        if (table[i].param_str[0] != '\0') {
            cJSON_AddStringToObject(entry, "param_str", table[i].param_str);
        }
        cJSON_AddNumberToObject(entry, "rx", table[i].rx);
        cJSON_AddItemToArray(arr, entry);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ----- REST API: PUT /api/mappings -----

static esp_err_t api_mappings_put_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char *buf = malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total_len] = '\0';

    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected JSON array");
        return ESP_FAIL;
    }

    // Clear and rebuild mapping table
    mapping_engine_reset_defaults();

    int applied = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        cJSON *ctrl = cJSON_GetObjectItem(item, "control");
        cJSON *action = cJSON_GetObjectItem(item, "action");
        if (!ctrl || !cJSON_IsString(ctrl) || !action || !cJSON_IsNumber(action)) continue;

        mapping_entry_t entry = {0};
        strncpy(entry.control_name, ctrl->valuestring, sizeof(entry.control_name) - 1);
        entry.action = (mapping_action_t)action->valueint;

        cJSON *pi = cJSON_GetObjectItem(item, "param_int");
        if (pi && cJSON_IsNumber(pi)) entry.param_int = pi->valueint;

        cJSON *ps = cJSON_GetObjectItem(item, "param_str");
        if (ps && cJSON_IsString(ps)) {
            strncpy(entry.param_str, ps->valuestring, sizeof(entry.param_str) - 1);
        }

        cJSON *rx = cJSON_GetObjectItem(item, "rx");
        if (rx && cJSON_IsNumber(rx)) entry.rx = (uint8_t)rx->valueint;

        if (mapping_engine_set(&entry) == ESP_OK) applied++;
    }

    cJSON_Delete(arr);

    // Save to NVS
    esp_err_t save_ret = mapping_engine_save();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", save_ret == ESP_OK);
    cJSON_AddNumberToObject(resp, "applied", applied);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ----- REST API: POST /api/mappings/reset -----

static esp_err_t api_mappings_reset_handler(httpd_req_t *req)
{
    mapping_engine_reset_defaults();
    esp_err_t ret = mapping_engine_save();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", ret == ESP_OK);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ----- Static file serving from SPIFFS -----

static const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".woff") == 0) return "font/woff";
    if (strcmp(ext, ".woff2") == 0) return "font/woff2";
    return "application/octet-stream";
}

static esp_err_t static_file_handler(httpd_req_t *req)
{
    char filepath[128];
    const char *uri = req->uri;

    // Strip query string
    const char *query = strchr(uri, '?');
    size_t uri_len = query ? (size_t)(query - uri) : strlen(uri);

    if (uri_len == 1 && uri[0] == '/') {
        snprintf(filepath, sizeof(filepath), "/www/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/www%.*s", (int)uri_len, uri);
    }

    // Try the exact path first
    struct stat st;
    if (stat(filepath, &st) != 0) {
        // SPA fallback: serve index.html for non-file paths
        snprintf(filepath, sizeof(filepath), "/www/index.html");
        if (stat(filepath, &st) != 0) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_mime_type(filepath));

    // Enable caching for static assets (not index.html)
    if (strstr(filepath, "index.html") == NULL) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    }

    char buf[512];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ----- Socket close handler (cleanup WS clients) -----

static void on_sock_close(httpd_handle_t hd, int sockfd)
{
    ws_remove_client(sockfd);
    close(sockfd);
}

// ----- SPIFFS mount/unmount -----

static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/www",
        .partition_label = "www",
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "SPIFFS partition 'www' not found");
        } else {
            ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("www", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %d/%d bytes used", used, total);
    return ESP_OK;
}

// ----- Server init/stop -----

esp_err_t http_server_init(void)
{
    // Mount SPIFFS for static files (non-fatal if no partition/files yet)
    esp_err_t spiffs_ret = mount_spiffs();
    if (spiffs_ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS not available - static file serving disabled");
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = on_sock_close;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // WebSocket endpoint
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    // REST API endpoints
    httpd_uri_t api_uris[] = {
        { .uri = "/api/status",         .method = HTTP_GET,  .handler = api_status_get_handler },
        { .uri = "/api/config",         .method = HTTP_GET,  .handler = api_config_get_handler },
        { .uri = "/api/config",         .method = HTTP_PUT,  .handler = api_config_put_handler },
        { .uri = "/api/mappings",       .method = HTTP_GET,  .handler = api_mappings_get_handler },
        { .uri = "/api/mappings",       .method = HTTP_PUT,  .handler = api_mappings_put_handler },
        { .uri = "/api/mappings/reset", .method = HTTP_POST, .handler = api_mappings_reset_handler },
    };
    for (int i = 0; i < sizeof(api_uris) / sizeof(api_uris[0]); i++) {
        httpd_register_uri_handler(s_server, &api_uris[i]);
    }

    // Static file catch-all (must be registered last due to wildcard)
    if (spiffs_ret == ESP_OK) {
        httpd_uri_t static_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = static_file_handler,
        };
        httpd_register_uri_handler(s_server, &static_uri);
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        s_ws_count = 0;
    }
    esp_vfs_spiffs_unregister("www");
    ESP_LOGI(TAG, "HTTP server stopped");
}
