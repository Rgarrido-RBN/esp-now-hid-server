#include "config_manager.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "CONFIG";
#define NVS_NAMESPACE "clutch"

static const clutch_config_t s_defaults = {
    .bite_point   = 40,
    .bite_hold_ms = 150,
    .release_ms   = 350,
    .release_curve = 1,
    .left_curve   = 20,
    .right_curve  = 20,
};

esp_err_t config_manager_init(void)
{
    /* NVS flash is initialized by espnow_handler_init() before this is called. */
    ESP_LOGI(TAG, "Config manager ready (namespace: \"%s\")", NVS_NAMESPACE);
    return ESP_OK;
}

void config_manager_load(clutch_config_t *cfg)
{
    *cfg = s_defaults;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        return;
    }

    uint8_t  v8;
    uint16_t v16;
    if (nvs_get_u8 (h, "bp", &v8)  == ESP_OK) cfg->bite_point   = v8;
    if (nvs_get_u16(h, "bh", &v16) == ESP_OK) cfg->bite_hold_ms = v16;
    if (nvs_get_u16(h, "rm", &v16) == ESP_OK) cfg->release_ms   = v16;
    if (nvs_get_u8 (h, "rc", &v8)  == ESP_OK) cfg->release_curve = v8;
    if (nvs_get_u8 (h, "cl", &v8)  == ESP_OK) cfg->left_curve   = v8;
    if (nvs_get_u8 (h, "cr", &v8)  == ESP_OK) cfg->right_curve  = v8;

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded: bite_point=%d hold=%dms release=%dms curve=%d paddle_curves=%d/%d",
             cfg->bite_point, cfg->bite_hold_ms, cfg->release_ms, cfg->release_curve,
             cfg->left_curve, cfg->right_curve);
}

void config_manager_save(const clutch_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }

    nvs_set_u8 (h, "bp", cfg->bite_point);
    nvs_set_u16(h, "bh", cfg->bite_hold_ms);
    nvs_set_u16(h, "rm", cfg->release_ms);
    nvs_set_u8 (h, "rc", cfg->release_curve);
    nvs_set_u8 (h, "cl", cfg->left_curve);
    nvs_set_u8 (h, "cr", cfg->right_curve);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Config saved: bite_point=%d hold=%dms release=%dms curve=%d paddle_curves=%d/%d",
             cfg->bite_point, cfg->bite_hold_ms, cfg->release_ms, cfg->release_curve,
             cfg->left_curve, cfg->right_curve);
}
