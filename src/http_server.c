#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "cJSON.h"

#include "http_server.h"
#include "config_store.h"
#include "event_engine.h"
#include "usb_dj_host.h"
#include "wifi_manager.h"

static const char *TAG = "http_srv";

static httpd_handle_t s_server = NULL;

// ----- WebSocket client tracking -----

#define MAX_WS_CLIENTS 4

static int s_ws_fds[MAX_WS_CLIENTS];
static int s_ws_count = 0;

static void ws_add_client(int fd) {
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) return;
    }
    if (s_ws_count < MAX_WS_CLIENTS) {
        s_ws_fds[s_ws_count++] = fd;
        ESP_LOGI(TAG, "WS client connected (fd=%d, total=%d)", fd, s_ws_count);
    }
}

static void ws_remove_client(int fd) {
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = s_ws_fds[--s_ws_count];
            ESP_LOGI(TAG, "WS client disconnected (fd=%d, total=%d)", fd, s_ws_count);
            return;
        }
    }
}

void http_server_ws_broadcast(const char *json) {
    if (!s_server || s_ws_count == 0) return;

    httpd_ws_frame_t pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    for (int i = 0; i < s_ws_count; ) {
        esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &pkt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WS send failed fd=%d, removing", s_ws_fds[i]);
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
    uint8_t new_value) {
    if (s_ws_count == 0) return;

    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"control\",\"name\":\"%s\",\"ctrl\":%d,\"old\":%d,\"new\":%d}",
        name, (int)control_type, old_value, new_value);
    http_server_ws_broadcast(buf);
}

void http_server_notify_status(void) {
    if (s_ws_count == 0) return;

    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"status\",\"usb\":%s,\"heap\":%lu}",
        usb_dj_host_is_connected() ? "true" : "false",
        esp_get_free_heap_size());
    http_server_ws_broadcast(buf);
}

// ----- WebSocket handler -----

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ws_add_client(httpd_req_to_sockfd(req));
        http_server_notify_status();
        return ESP_OK;
    }

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
        }
        free(buf);
    }
    return ret;
}

// ----- REST API: GET /api/status -----

static esp_err_t api_status_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    cJSON_AddBoolToObject(root, "usb_connected", usb_dj_host_is_connected());
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_manager_is_connected());
    cJSON_AddBoolToObject(root, "ap_mode", wifi_manager_is_ap_mode());
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

static esp_err_t api_config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    char val[64];

    if (config_get_str(CFG_KEY_WIFI_SSID, val, sizeof(val)) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_ssid", val);
    } else {
        cJSON_AddStringToObject(root, "wifi_ssid", "");
    }
    cJSON_AddBoolToObject(root, "wifi_pass_set",
        config_get_str(CFG_KEY_WIFI_PASS, val, sizeof(val)) == ESP_OK && val[0] != '\0');

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

static esp_err_t api_config_put_handler(httpd_req_t *req) {
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
        if (ret <= 0) { free(buf); return ESP_FAIL; }
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

    cJSON *item = cJSON_GetObjectItem(root, "wifi_ssid");
    if (item && cJSON_IsString(item)) {
        config_set_str(CFG_KEY_WIFI_SSID, item->valuestring);
        need_wifi_restart = true;
    }

    item = cJSON_GetObjectItem(root, "wifi_pass");
    if (item && cJSON_IsString(item)) {
        config_set_str(CFG_KEY_WIFI_PASS, item->valuestring);
        need_wifi_restart = true;
    }

    item = cJSON_GetObjectItem(root, "debug_level");
    if (item && cJSON_IsNumber(item)) {
        config_set_u8(CFG_KEY_DEBUG_LEVEL, (uint8_t)item->valueint);
    }

    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "restart_required", need_wifi_restart);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    if (need_wifi_restart) {
        char ssid[64] = {0}, pass[64] = {0};
        config_get_str(CFG_KEY_WIFI_SSID, ssid, sizeof(ssid));
        config_get_str(CFG_KEY_WIFI_PASS, pass, sizeof(pass));
        if (ssid[0] != '\0') {
            wifi_manager_set_credentials(ssid, pass);
        }
    }

    return ESP_OK;
}

// ----- REST API: GET /api/mappings -----

static esp_err_t api_mappings_get_handler(httpd_req_t *req) {
    int count = 0;
    const midi_mapping_t *table = event_engine_get_mappings(&count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "control_id", table[i].control_id);
        cJSON_AddNumberToObject(entry, "channel", table[i].midi_channel);
        cJSON_AddNumberToObject(entry, "type", table[i].midi_type);
        cJSON_AddNumberToObject(entry, "param", table[i].midi_param);
        cJSON_AddNumberToObject(entry, "scale", table[i].scale);
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

static esp_err_t api_mappings_put_handler(httpd_req_t *req) {
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
        if (ret <= 0) { free(buf); return ESP_FAIL; }
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

    int applied = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        cJSON *cid = cJSON_GetObjectItem(item, "control_id");
        cJSON *ch  = cJSON_GetObjectItem(item, "channel");
        cJSON *ty  = cJSON_GetObjectItem(item, "type");
        cJSON *pm  = cJSON_GetObjectItem(item, "param");
        cJSON *sc  = cJSON_GetObjectItem(item, "scale");
        if (!cid || !ch || !ty || !pm) continue;

        midi_mapping_t m = {
            .control_id   = (uint8_t)cid->valueint,
            .midi_channel = (uint8_t)ch->valueint,
            .midi_type    = (uint8_t)ty->valueint,
            .midi_param   = (uint8_t)pm->valueint,
            .scale        = (sc && cJSON_IsNumber(sc)) ? (float)sc->valuedouble : 0.5f,
        };
        if (event_engine_set_mapping(&m) == ESP_OK) applied++;
    }
    cJSON_Delete(arr);

    esp_err_t save_ret = event_engine_save();

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

static esp_err_t api_mappings_reset_handler(httpd_req_t *req) {
    // Re-init loads defaults if NVS is empty
    event_engine_init();
    esp_err_t ret = event_engine_save();

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

static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".woff2") == 0) return "font/woff2";
    return "application/octet-stream";
}

static esp_err_t static_file_handler(httpd_req_t *req);

static esp_err_t captive_portal_handler(httpd_req_t *req) {
    if (wifi_manager_is_ap_mode()) {
        char host[64] = {0};
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
        if (host[0] != '\0'
            && strcmp(host, "192.168.4.1") != 0
            && strcmp(host, "djconsole.local") != 0
            && strncmp(host, "192.168.4.1:", 12) != 0) {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
            httpd_resp_sendstr(req, "Redirecting to MIDI Bridge setup...");
            return ESP_OK;
        }
    }
    return static_file_handler(req);
}

static bool client_accepts_gzip(httpd_req_t *req) {
    char accept[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", accept, sizeof(accept)) == ESP_OK) {
        return (strstr(accept, "gzip") != NULL);
    }
    return false;
}

static esp_err_t static_file_handler(httpd_req_t *req) {
    char filepath[128];
    const char *uri = req->uri;

    const char *query = strchr(uri, '?');
    size_t uri_len = query ? (size_t)(query - uri) : strlen(uri);

    if (uri_len == 1 && uri[0] == '/') {
        snprintf(filepath, sizeof(filepath), "/www/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/www%.*s", (int)uri_len, uri);
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        snprintf(filepath, sizeof(filepath), "/www/index.html");
        if (stat(filepath, &st) != 0) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }
    }

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

static void on_sock_close(httpd_handle_t hd, int sockfd) {
    ws_remove_client(sockfd);
    close(sockfd);
}

// ----- SPIFFS mount -----

static esp_err_t mount_spiffs(void) {
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

esp_err_t http_server_init(void) {
    esp_err_t spiffs_ret = mount_spiffs();
    if (spiffs_ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS not available - static file serving disabled");
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = on_sock_close;
    config.stack_size = 12288;

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

    // REST API
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

void http_server_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        s_ws_count = 0;
    }
    esp_vfs_spiffs_unregister("www");
    ESP_LOGI(TAG, "HTTP server stopped");
}
