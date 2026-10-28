#include "web_config.h"
#include "config_manager.h"
#include "clutch_engine.h"
#include "data_processor.h"
#include "shared_state.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "WEBCONFIG";

/* AP credentials — change these two defines to customise */
#define AP_SSID     "VirtualClutch"
#define AP_PASSWORD "iracing123"
#define AP_CHANNEL  1          /* must match ESP-NOW channel */
#define AP_MAX_CONN 3

#define MAX_BODY_LEN 2048

/* Pointer to the live config (owned by main.c) */
static device_config_t *s_cfg = NULL;

/* Embedded HTML page (via CMakeLists EMBED_TXTFILES) */
extern const uint8_t web_page_html_start[] asm("_binary_web_page_html_start");
extern const uint8_t web_page_html_end[]   asm("_binary_web_page_html_end");

/* ------------------------------------------------------------------ */
/* JSON helpers                                                        */
/* ------------------------------------------------------------------ */

/*
 * Config JSON:
 * {"paddles":[{"mode":0|1,"curve":10-40,"trigger":10-95,
 *              "points":[[t_ms,value_pct],...]}, ...]}
 */
static cJSON *config_to_json(const device_config_t *cfg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *paddles = cJSON_AddArrayToObject(root, "paddles");

    for (int i = 0; i < PADDLE_COUNT; i++) {
        const paddle_config_t *p = &cfg->paddle[i];
        cJSON *jp = cJSON_CreateObject();
        cJSON_AddNumberToObject(jp, "mode", p->mode);
        cJSON_AddNumberToObject(jp, "curve", p->curve_x10);
        cJSON_AddNumberToObject(jp, "trigger", p->trigger_pct);
        cJSON *pts = cJSON_AddArrayToObject(jp, "points");
        for (int k = 0; k < p->num_points; k++) {
            int pair[2] = { p->points[k].t_ms, p->points[k].value };
            cJSON_AddItemToArray(pts, cJSON_CreateIntArray(pair, 2));
        }
        cJSON_AddItemToArray(paddles, jp);
    }
    return root;
}

static int json_int(const cJSON *obj, const char *key, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) return -1;
    *out = item->valueint;
    return 0;
}

/* Parses the config JSON into *cfg. Returns 0 on success. */
static int json_to_config(const cJSON *root, device_config_t *cfg)
{
    const cJSON *paddles = cJSON_GetObjectItemCaseSensitive(root, "paddles");
    if (!cJSON_IsArray(paddles) || cJSON_GetArraySize(paddles) != PADDLE_COUNT) return -1;

    for (int i = 0; i < PADDLE_COUNT; i++) {
        const cJSON *jp = cJSON_GetArrayItem(paddles, i);
        paddle_config_t *p = &cfg->paddle[i];
        int mode, curve, trigger;

        if (json_int(jp, "mode", &mode) != 0 ||
            json_int(jp, "curve", &curve) != 0 ||
            json_int(jp, "trigger", &trigger) != 0) {
            return -1;
        }
        p->mode        = (uint8_t)mode;
        p->curve_x10   = (uint8_t)(curve < 0 ? 0 : curve > 255 ? 255 : curve);
        p->trigger_pct = (uint8_t)(trigger < 0 ? 0 : trigger > 255 ? 255 : trigger);

        const cJSON *pts = cJSON_GetObjectItemCaseSensitive(jp, "points");
        int n = cJSON_GetArraySize(pts);
        if (!cJSON_IsArray(pts) || n < 2 || n > RELEASE_MAX_POINTS) return -1;

        for (int k = 0; k < n; k++) {
            const cJSON *pair = cJSON_GetArrayItem(pts, k);
            const cJSON *t = cJSON_GetArrayItem(pair, 0);
            const cJSON *v = cJSON_GetArrayItem(pair, 1);
            if (!cJSON_IsNumber(t) || !cJSON_IsNumber(v)) return -1;
            int tv = t->valueint, vv = v->valueint;
            p->points[k].t_ms  = (uint16_t)(tv < 0 ? 0 : tv > RELEASE_MAX_MS ? RELEASE_MAX_MS : tv);
            p->points[k].value = (uint8_t)(vv < 0 ? 0 : vv > 100 ? 100 : vv);
        }
        p->num_points = (uint8_t)n;
    }
    return 0;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
    free(str);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

static esp_err_t handler_get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
                    (const char *)web_page_html_start,
                    (ssize_t)(web_page_html_end - web_page_html_start));
    return ESP_OK;
}

static esp_err_t handler_get_config(httpd_req_t *req)
{
    return send_json(req, config_to_json(s_cfg));
}

static esp_err_t handler_post_config(httpd_req_t *req)
{
    int body_len = req->content_len;
    if (body_len <= 0 || body_len >= MAX_BODY_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad content length");
        return ESP_FAIL;
    }

    char *body = malloc(body_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < body_len) {
        int r = httpd_req_recv(req, body + received, body_len - received);
        if (r <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);

    device_config_t new_cfg = *s_cfg;
    int err = root ? json_to_config(root, &new_cfg) : -1;
    cJSON_Delete(root);
    if (err != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config");
        return ESP_FAIL;
    }
    config_manager_sanitize(&new_cfg);

    /* Persist then apply without restart */
    config_manager_save(&new_cfg);
    *s_cfg = new_cfg;
    clutch_engine_reload_config(&new_cfg);
    data_processor_set_curves(new_cfg.paddle[0].curve_x10, new_cfg.paddle[1].curve_x10);

    /* Echo back the sanitized config so the page shows what was applied */
    return send_json(req, config_to_json(&new_cfg));
}

/*
 * Status JSON:
 * {"paddles":[{"input":0-65535,"output":0-65535,"state":"idle|pressed|releasing",
 *              "elapsed":ms}, ...]}
 */
static esp_err_t handler_get_status(httpd_req_t *req)
{
    static const char *state_names[] = {"idle", "pressed", "releasing"};

    char buf[256];
    int len = snprintf(buf, sizeof(buf), "{\"paddles\":[");
    for (int i = 0; i < PADDLE_COUNT; i++) {
        clutch_status_t st = clutch_engine_get_status(i);
        len += snprintf(buf + len, sizeof(buf) - len,
                        "%s{\"input\":%u,\"output\":%u,\"state\":\"%s\",\"elapsed\":%lu}",
                        i ? "," : "",
                        g_paddle_input[i], g_paddle_output[i],
                        (st.state <= CE_RELEASING) ? state_names[st.state] : "idle",
                        (unsigned long)st.elapsed_ms);
    }
    snprintf(buf + len, sizeof(buf) - len, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t web_config_init(device_config_t *cfg)
{
    s_cfg = cfg;

    /* Configure WiFi AP (WiFi was already started in APSTA mode by
     * espnow_handler_init; we just configure the AP interface here). */
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = sizeof(AP_SSID) - 1,
            .password       = AP_PASSWORD,
            .channel        = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    ESP_LOGI(TAG, "WiFi AP: SSID=\"%s\"  IP=192.168.4.1", AP_SSID);

    /* Start HTTP server */
    httpd_config_t http_cfg  = HTTPD_DEFAULT_CONFIG();
    http_cfg.stack_size      = 8192;
    http_cfg.server_port     = 80;
    http_cfg.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    /* Register URI handlers */
    httpd_uri_t uris[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = handler_get_root   },
        { .uri = "/config", .method = HTTP_GET,  .handler = handler_get_config },
        { .uri = "/config", .method = HTTP_POST, .handler = handler_post_config },
        { .uri = "/status", .method = HTTP_GET,  .handler = handler_get_status },
    };
    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}
