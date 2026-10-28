#include "config_manager.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CONFIG";
#define NVS_NAMESPACE "clutch"
#define NVS_KEY       "cfg"
#define CFG_VERSION   2     // bump when device_config_t layout changes

/* Stored blob: version byte + config, so an old layout is never misread */
typedef struct {
    uint8_t         version;
    device_config_t cfg;
} stored_config_t;

static const paddle_config_t s_paddle_default = {
    .mode        = PADDLE_MODE_AXIS,
    .curve_x10   = 20,
    .trigger_pct = 50,
    .num_points  = 3,
    .points      = { { 0, 40 }, { 150, 40 }, { 500, 0 } },
};

esp_err_t config_manager_init(void)
{
    /* NVS flash is initialized by espnow_handler_init() before this is called. */
    ESP_LOGI(TAG, "Config manager ready (namespace: \"%s\")", NVS_NAMESPACE);
    return ESP_OK;
}

void config_manager_defaults(device_config_t *cfg)
{
    for (int i = 0; i < PADDLE_COUNT; i++) {
        cfg->paddle[i] = s_paddle_default;
    }
}

static uint8_t clamp_u8(int v, int lo, int hi)
{
    return (uint8_t)(v < lo ? lo : v > hi ? hi : v);
}

void config_manager_sanitize(device_config_t *cfg)
{
    for (int i = 0; i < PADDLE_COUNT; i++) {
        paddle_config_t *p = &cfg->paddle[i];

        p->mode        = (p->mode == PADDLE_MODE_RELEASE) ? PADDLE_MODE_RELEASE : PADDLE_MODE_AXIS;
        p->curve_x10   = clamp_u8(p->curve_x10, 10, 40);
        p->trigger_pct = clamp_u8(p->trigger_pct, 10, 95);

        if (p->num_points < 2 || p->num_points > RELEASE_MAX_POINTS) {
            memcpy(p->points, s_paddle_default.points, sizeof(p->points));
            p->num_points = s_paddle_default.num_points;
        }

        /* First point at 0 ms, times strictly increasing, last point at 0 % */
        p->points[0].t_ms = 0;
        for (int k = 0; k < p->num_points; k++) {
            release_point_t *pt = &p->points[k];
            pt->value = clamp_u8(pt->value, 0, 100);
            if (k > 0 && pt->t_ms <= p->points[k - 1].t_ms) {
                pt->t_ms = p->points[k - 1].t_ms + 1;
            }
            if (pt->t_ms > RELEASE_MAX_MS) pt->t_ms = RELEASE_MAX_MS;
        }
        p->points[p->num_points - 1].value = 0;
    }
}

static void log_config(const char *what, const device_config_t *cfg)
{
    for (int i = 0; i < PADDLE_COUNT; i++) {
        const paddle_config_t *p = &cfg->paddle[i];
        ESP_LOGI(TAG, "%s paddle %d: mode=%s curve=^%.1f trigger=%d%% points=%d (%d ms)",
                 what, i, p->mode == PADDLE_MODE_RELEASE ? "release" : "axis",
                 p->curve_x10 / 10.0f, p->trigger_pct, p->num_points,
                 p->points[p->num_points - 1].t_ms);
    }
}

void config_manager_load(device_config_t *cfg)
{
    config_manager_defaults(cfg);

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        return;
    }

    stored_config_t stored;
    size_t len = sizeof(stored);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, &stored, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(stored) || stored.version != CFG_VERSION) {
        ESP_LOGI(TAG, "No valid saved config, using defaults");
        return;
    }

    *cfg = stored.cfg;
    config_manager_sanitize(cfg);
    log_config("Loaded", cfg);
}

void config_manager_save(const device_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }

    stored_config_t stored = { .version = CFG_VERSION, .cfg = *cfg };
    nvs_set_blob(h, NVS_KEY, &stored, sizeof(stored));
    nvs_commit(h);
    nvs_close(h);

    log_config("Saved", cfg);
}
