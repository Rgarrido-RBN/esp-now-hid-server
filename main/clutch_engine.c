#include "clutch_engine.h"
#include "shared_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <math.h>
#include "esp_log.h"

static const char *TAG = "CLUTCH";

/* Shared globals - defined here, declared extern in shared_state.h */
volatile bool     g_left_clutch_pressed  = false;
volatile uint16_t g_virtual_clutch_value = 0;

static clutch_config_t   s_cfg;
static clutch_state_t    s_state = CE_IDLE;
static portMUX_TYPE      s_mux   = portMUX_INITIALIZER_UNLOCKED;

/* Scale a bite-point percentage (5-80) to a 0-65535 axis value. */
static inline uint16_t bp_to_axis(uint8_t bp_pct)
{
    return (uint16_t)((uint32_t)bp_pct * 65535u / 100u);
}

void clutch_engine_init(const clutch_config_t *cfg)
{
    taskENTER_CRITICAL(&s_mux);
    s_cfg  = *cfg;
    s_state = CE_IDLE;
    taskEXIT_CRITICAL(&s_mux);

    g_left_clutch_pressed  = false;
    g_virtual_clutch_value = 0;

    ESP_LOGI(TAG, "Clutch engine initialized");
}

void clutch_engine_reload_config(const clutch_config_t *cfg)
{
    taskENTER_CRITICAL(&s_mux);
    s_cfg = *cfg;
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Config reloaded: bite_point=%d hold=%dms release=%dms curve=%d",
             cfg->bite_point, cfg->bite_hold_ms, cfg->release_ms, cfg->release_curve);
}

clutch_state_t clutch_engine_get_state(void)
{
    return s_state;
}

void task_clutch_engine(void *arg)
{
    uint32_t timer_ms          = 0;
    uint16_t release_start_val = 0;  /* axis value at the moment RELEASING began */

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Snapshot config under critical section to avoid torn reads */
        clutch_config_t cfg;
        taskENTER_CRITICAL(&s_mux);
        cfg = s_cfg;
        taskEXIT_CRITICAL(&s_mux);

        bool     pressed        = g_left_clutch_pressed;
        uint16_t bite_axis_val  = bp_to_axis(cfg.bite_point);

        switch (s_state) {
            case CE_IDLE:
                g_virtual_clutch_value = 0;
                if (pressed) {
                    s_state = CE_PRESSED;
                    g_virtual_clutch_value = 65535;
                    ESP_LOGD(TAG, "IDLE -> PRESSED");
                }
                break;

            case CE_PRESSED:
                g_virtual_clutch_value = 65535;
                if (!pressed) {
                    s_state            = CE_BITE_HOLD;
                    g_virtual_clutch_value = bite_axis_val;
                    timer_ms           = 0;
                    ESP_LOGD(TAG, "PRESSED -> BITE_HOLD (val=%d)", bite_axis_val);
                }
                break;

            case CE_BITE_HOLD:
                g_virtual_clutch_value = bite_axis_val;
                timer_ms += 10;
                if (pressed) {
                    /* Re-engage during hold */
                    s_state = CE_PRESSED;
                    g_virtual_clutch_value = 65535;
                    timer_ms = 0;
                    ESP_LOGD(TAG, "BITE_HOLD -> PRESSED (re-engage)");
                } else if (timer_ms >= cfg.bite_hold_ms) {
                    s_state           = CE_RELEASING;
                    release_start_val = bite_axis_val;
                    timer_ms          = 0;
                    ESP_LOGD(TAG, "BITE_HOLD -> RELEASING");
                }
                break;

            case CE_RELEASING:
                timer_ms += 10;
                if (pressed) {
                    /* Re-engage during release */
                    s_state = CE_PRESSED;
                    g_virtual_clutch_value = 65535;
                    timer_ms = 0;
                    ESP_LOGD(TAG, "RELEASING -> PRESSED (re-engage)");
                } else if (timer_ms >= cfg.release_ms) {
                    s_state = CE_IDLE;
                    g_virtual_clutch_value = 0;
                    timer_ms = 0;
                    ESP_LOGD(TAG, "RELEASING -> IDLE");
                } else {
                    float progress = (float)timer_ms / (float)cfg.release_ms;
                    float eased;
                    switch (cfg.release_curve) {
                        case 1:  eased = powf(progress, 1.5f); break;  /* smooth  */
                        case 2:  eased = powf(progress, 0.6f); break;  /* aggressive */
                        default: eased = progress;             break;  /* linear  */
                    }
                    float val = (float)release_start_val * (1.0f - eased);
                    g_virtual_clutch_value = (val < 0.0f) ? 0u : (uint16_t)val;
                }
                break;
        }
    }
}
