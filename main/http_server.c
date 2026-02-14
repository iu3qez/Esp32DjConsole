#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "cJSON.h"

#include "http_server.h"
#include "config_store.h"
#include "mapping_engine.h"
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

void http_server_notify_status(void)
{
    if (s_ws_count == 0) return;

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
        "{\"type\":\"status\",\"usb\":%s,\"cat\":\"%s\",\"heap\":%lu}",
        usb_dj_host_is_connected() ? "true" : "false",
        cat_str,
        esp_get_free_heap_size());
    http_server_ws_broadcast(buf);
}

// ----- Learn mode callback (fires when a control is learned) -----

static void on_learn_complete(const char *control_name, uint16_t command_id, const char *command_name)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"learned\",\"control\":\"%s\",\"command_id\":%d,\"command_name\":\"%s\"}",
        control_name, command_id, command_name);
    http_server_ws_broadcast(buf);
}

// ----- WebSocket handler -----

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ws_add_client(httpd_req_to_sockfd(req));
        http_server_notify_status();
        return ESP_OK;
    }

    // Receive a frame
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

            // Parse incoming JSON commands
            cJSON *msg = cJSON_Parse((char *)buf);
            if (msg) {
                cJSON *type = cJSON_GetObjectItem(msg, "type");
                if (type && cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "learn") == 0) {
                        cJSON *cmd_id = cJSON_GetObjectItem(msg, "command_id");
                        if (cmd_id && cJSON_IsNumber(cmd_id)) {
                            mapping_engine_start_learn((uint16_t)cmd_id->valueint);
                        }
                    } else if (strcmp(type->valuestring, "learn_cancel") == 0) {
                        mapping_engine_cancel_learn();
                    }
                }
                cJSON_Delete(msg);
            }
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

    // CAT state
    const char *cat_states[] = {"disconnected","connecting","connected","error"};
    cat_state_t cs = cat_client_get_state();
    cJSON_AddStringToObject(root, "cat_state",
        (cs >= 0 && cs <= CAT_STATE_ERROR) ? cat_states[cs] : "unknown");

    // WiFi
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_manager_is_connected());
    cJSON_AddBoolToObject(root, "ap_mode", wifi_manager_is_ap_mode());

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
    cJSON_AddBoolToObject(root, "wifi_pass_set",
        config_get_str(CFG_KEY_WIFI_PASS, val, sizeof(val)) == ESP_OK && val[0] != '\0');

    // CAT
    val[0] = '\0';
    config_get_str(CFG_KEY_CAT_HOST, val, sizeof(val));
    cJSON_AddStringToObject(root, "cat_host", val);
    uint16_t port = 31001;
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
    bool need_reconnect = false;

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

    // CAT host/port
    item = cJSON_GetObjectItem(root, "cat_host");
    if (item && cJSON_IsString(item)) {
        config_set_str(CFG_KEY_CAT_HOST, item->valuestring);
        need_reconnect = true;
    }
    item = cJSON_GetObjectItem(root, "cat_port");
    if (item && cJSON_IsNumber(item)) {
        config_set_u16(CFG_KEY_CAT_PORT, (uint16_t)item->valueint);
        need_reconnect = true;
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
    cJSON_AddBoolToObject(resp, "restart_required", need_wifi_restart || need_reconnect);
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

// ----- REST API: GET /api/commands -----

static esp_err_t api_commands_get_handler(httpd_req_t *req)
{
    int count = 0;
    const thetis_cmd_t *db = cmd_db_get_all(&count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "id", db[i].id);
        cJSON_AddStringToObject(obj, "name", db[i].name);
        cJSON_AddNumberToObject(obj, "cat", db[i].category);
        cJSON_AddStringToObject(obj, "cat_name", cmd_category_name(db[i].category));
        cJSON_AddNumberToObject(obj, "exec", db[i].exec_type);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
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
        cJSON_AddStringToObject(entry, "c", table[i].control_name);
        cJSON_AddNumberToObject(entry, "id", table[i].command_id);
        if (table[i].param != 0) {
            cJSON_AddNumberToObject(entry, "p", table[i].param);
        }
        // Include command name for UI convenience
        const thetis_cmd_t *cmd = cmd_db_find(table[i].command_id);
        if (cmd) {
            cJSON_AddStringToObject(entry, "name", cmd->name);
        }
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
        cJSON *c = cJSON_GetObjectItem(item, "c");
        cJSON *id = cJSON_GetObjectItem(item, "id");
        if (!c || !cJSON_IsString(c) || !id || !cJSON_IsNumber(id)) continue;

        mapping_entry_t entry = {0};
        strncpy(entry.control_name, c->valuestring, sizeof(entry.control_name) - 1);
        entry.command_id = (uint16_t)id->valuedouble;

        cJSON *p = cJSON_GetObjectItem(item, "p");
        if (p && cJSON_IsNumber(p)) entry.param = (int32_t)p->valuedouble;

        if (cmd_db_find(entry.command_id) && mapping_engine_set(&entry) == ESP_OK) {
            applied++;
        }
    }

    cJSON_Delete(arr);

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

// ----- REST API: GET /api/mappings/download -----

static esp_err_t api_mappings_download_handler(httpd_req_t *req)
{
    struct stat st;
    if (stat("/www/mappings.json", &st) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No mappings file");
        return ESP_FAIL;
    }

    FILE *f = fopen("/www/mappings.json", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"mappings.json\"");

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ----- REST API: POST /api/mappings/upload -----

static esp_err_t api_mappings_upload_handler(httpd_req_t *req)
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

    // Validate JSON
    cJSON *arr = cJSON_Parse(buf);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON array");
        return ESP_FAIL;
    }
    cJSON_Delete(arr);

    // Write to SPIFFS
    FILE *f = fopen("/www/mappings.json", "w");
    if (!f) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file");
        return ESP_FAIL;
    }
    fwrite(buf, 1, total_len, f);
    fclose(f);
    free(buf);

    // Reload mappings from the file
    mapping_engine_load();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    int count = 0;
    mapping_engine_get_table(&count);
    cJSON_AddNumberToObject(resp, "loaded", count);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// ----- REST API: DELETE /api/mappings/:control -----

static esp_err_t api_mapping_delete_handler(httpd_req_t *req)
{
    // URI is /api/mappings/clear?c=<control_name>
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query param ?c=name");
        return ESP_FAIL;
    }

    char control[24] = {0};
    if (httpd_query_key_value(query, "c", control, sizeof(control)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ?c= parameter");
        return ESP_FAIL;
    }

    esp_err_t ret = mapping_engine_remove(control);
    if (ret == ESP_OK) {
        mapping_engine_save();
    }

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

static esp_err_t static_file_handler_internal(httpd_req_t *req);

// Captive portal detection
static esp_err_t captive_portal_handler(httpd_req_t *req)
{
    if (wifi_manager_is_ap_mode()) {
        char host[64] = {0};
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
        if (host[0] != '\0'
            && strcmp(host, "192.168.4.1") != 0
            && strcmp(host, "djconsole.local") != 0
            && strncmp(host, "192.168.4.1:", 12) != 0) {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
            httpd_resp_sendstr(req, "Redirecting to DJ Console setup...");
            return ESP_OK;
        }
    }
    return static_file_handler_internal(req);
}

static bool client_accepts_gzip(httpd_req_t *req)
{
    char accept[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", accept, sizeof(accept)) == ESP_OK) {
        return (strstr(accept, "gzip") != NULL);
    }
    return false;
}

static esp_err_t static_file_handler_internal(httpd_req_t *req)
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

    // Don't serve mappings.json as a static file (API handles it)
    if (strcmp(filepath, "/www/mappings.json") == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Use /api/mappings");
        return ESP_FAIL;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        // SPA fallback
        snprintf(filepath, sizeof(filepath), "/www/index.html");
        if (stat(filepath, &st) != 0) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }
    }

    // Try gzip version
    bool serving_gzip = false;
    char gzpath[132];
    if (client_accepts_gzip(req)) {
        snprintf(gzpath, sizeof(gzpath), "%s.gz", filepath);
        if (stat(gzpath, &st) == 0) {
            serving_gzip = true;
        }
    }

    FILE *f = fopen(serving_gzip ? gzpath : filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_mime_type(filepath));

    if (serving_gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

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

// ----- Socket close handler -----

static void on_sock_close(httpd_handle_t hd, int sockfd)
{
    ws_remove_client(sockfd);
}

// ----- SPIFFS mount -----

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
    esp_err_t spiffs_ret = mount_spiffs();
    if (spiffs_ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS not available - static file serving disabled");
    }

    // Register learn mode callback
    mapping_engine_set_learn_callback(on_learn_complete);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
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
        { .uri = "/api/status",            .method = HTTP_GET,  .handler = api_status_get_handler },
        { .uri = "/api/config",            .method = HTTP_GET,  .handler = api_config_get_handler },
        { .uri = "/api/config",            .method = HTTP_PUT,  .handler = api_config_put_handler },
        { .uri = "/api/commands",          .method = HTTP_GET,  .handler = api_commands_get_handler },
        { .uri = "/api/mappings",          .method = HTTP_GET,  .handler = api_mappings_get_handler },
        { .uri = "/api/mappings",          .method = HTTP_PUT,  .handler = api_mappings_put_handler },
        { .uri = "/api/mappings/reset",    .method = HTTP_POST, .handler = api_mappings_reset_handler },
        { .uri = "/api/mappings/download", .method = HTTP_GET,  .handler = api_mappings_download_handler },
        { .uri = "/api/mappings/upload",   .method = HTTP_POST, .handler = api_mappings_upload_handler },
        { .uri = "/api/mappings/clear",    .method = HTTP_POST, .handler = api_mapping_delete_handler },
    };
    for (int i = 0; i < sizeof(api_uris) / sizeof(api_uris[0]); i++) {
        httpd_register_uri_handler(s_server, &api_uris[i]);
    }

    // Static file catch-all (must be last)
    if (spiffs_ret == ESP_OK) {
        httpd_uri_t static_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = captive_portal_handler,
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
