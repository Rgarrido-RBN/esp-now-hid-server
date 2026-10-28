#include "web_config.h"
#include "config_manager.h"
#include "clutch_engine.h"
#include "data_processor.h"
#include "shared_state.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "WEBCONFIG";

/* AP credentials — change these two defines to customise */
#define AP_SSID     "VirtualClutch"
#define AP_PASSWORD "iracing123"
#define AP_CHANNEL  1          /* must match ESP-NOW channel */
#define AP_MAX_CONN 3

/* Pointer to the live config (owned by main.c) */
static clutch_config_t *s_cfg = NULL;

/* Embedded HTML page (via CMakeLists EMBED_TXTFILES) */
extern const uint8_t web_page_html_start[] asm("_binary_web_page_html_start");
extern const uint8_t web_page_html_end[]   asm("_binary_web_page_html_end");

/* ------------------------------------------------------------------ */
/* JSON helpers                                                        */
/* ------------------------------------------------------------------ */

/* Read a single JSON integer field from a flat JSON object.
 * Returns 0 on success, -1 if the key was not found. */
static int json_get_int(const char *json, const char *key, int *out)
{
    /* Build search pattern: "key": or "key" : */
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    /* skip whitespace and colon */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return -1;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (sscanf(p, "%d", out) != 1) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                       */
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
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"bite_point\":%d,\"bite_hold_ms\":%d,"
             "\"release_ms\":%d,\"release_curve\":%d,"
             "\"left_curve\":%d,\"right_curve\":%d}",
             s_cfg->bite_point, s_cfg->bite_hold_ms,
             s_cfg->release_ms, s_cfg->release_curve,
             s_cfg->left_curve, s_cfg->right_curve);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handler_post_config(httpd_req_t *req)
{
    /* Read body (config JSON is small, 256 bytes is plenty) */
    char body[256];
    int  body_len = req->content_len;

    if (body_len <= 0 || body_len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad content length");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, body, body_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    /* Parse required fields */
    int bp, bh, rm, rc, lc, rcv;
    if (json_get_int(body, "bite_point",   &bp) != 0 ||
        json_get_int(body, "bite_hold_ms", &bh) != 0 ||
        json_get_int(body, "release_ms",   &rm) != 0 ||
        json_get_int(body, "release_curve",&rc) != 0 ||
        json_get_int(body, "left_curve",   &lc) != 0 ||
        json_get_int(body, "right_curve",  &rcv) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        return ESP_FAIL;
    }

    /* Clamp to valid ranges */
    clutch_config_t new_cfg = {
        .bite_point   = (uint8_t) (bp < 5   ? 5   : bp > 80   ? 80   : bp),
        .bite_hold_ms = (uint16_t)(bh < 50  ? 50  : bh > 800  ? 800  : bh),
        .release_ms   = (uint16_t)(rm < 100 ? 100 : rm > 2000 ? 2000 : rm),
        .release_curve= (uint8_t) (rc < 0   ? 0   : rc > 2    ? 2    : rc),
        .left_curve   = (uint8_t) (lc < 10  ? 10  : lc > 40   ? 40   : lc),
        .right_curve  = (uint8_t) (rcv < 10 ? 10  : rcv > 40  ? 40   : rcv),
    };

    /* Persist then apply without restart */
    config_manager_save(&new_cfg);
    *s_cfg = new_cfg;
    clutch_engine_reload_config(&new_cfg);
    data_processor_set_curves(new_cfg.left_curve, new_cfg.right_curve);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t handler_get_status(httpd_req_t *req)
{
    static const char *state_names[] = {"idle", "pressed", "bite_hold", "releasing"};
    clutch_state_t state = clutch_engine_get_state();

    /* g_virtual_clutch_value is read from shared_state.h */
    extern volatile uint16_t g_virtual_clutch_value;
    uint16_t axis_val = g_virtual_clutch_value;

    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"axis\":%d,\"state\":\"%s\",\"bite_point\":%d,"
             "\"left\":%d,\"right\":%d}",
             axis_val,
             (state < 4) ? state_names[state] : "idle",
             s_cfg->bite_point,
             g_left_clutch_value, g_right_clutch_value);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t web_config_init(clutch_config_t *cfg)
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
