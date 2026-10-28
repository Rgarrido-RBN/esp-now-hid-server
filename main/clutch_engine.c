#include "clutch_engine.h"
#include "shared_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_log.h"

static const char *TAG = "CLUTCH";

#define TICK_MS          10
#define HYSTERESIS_PCT   5     // release happens this much below the trigger

/* Shared globals - defined here, declared extern in shared_state.h */
volatile uint16_t g_paddle_output[PADDLE_COUNT] = {0};

static device_config_t  s_cfg;
static clutch_status_t  s_status[PADDLE_COUNT];
static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;

static inline uint16_t pct_to_axis(uint32_t pct)
{
    return (uint16_t)(pct * 65535u / 100u);
}

/* Output value (0-65535) of the release profile at t_ms after release. */
static uint16_t profile_value(const paddle_config_t *p, uint32_t t_ms)
{
    const release_point_t *pts = p->points;

    for (int k = 1; k < p->num_points; k++) {
        if (t_ms < pts[k].t_ms) {
            uint32_t span = pts[k].t_ms - pts[k - 1].t_ms;
            if (span == 0) return pct_to_axis(pts[k].value);
            int32_t dv = (int32_t)pts[k].value - (int32_t)pts[k - 1].value;
            int32_t v = (int32_t)pts[k - 1].value * 65535 / 100 +
                        dv * 65535 / 100 * (int32_t)(t_ms - pts[k - 1].t_ms) / (int32_t)span;
            return (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
        }
    }
    return pct_to_axis(pts[p->num_points - 1].value);
}

static uint16_t update_release(const paddle_config_t *p, clutch_status_t *st, uint16_t input)
{
    uint16_t on_level  = pct_to_axis(p->trigger_pct);
    uint16_t off_level = pct_to_axis(p->trigger_pct > HYSTERESIS_PCT ? p->trigger_pct - HYSTERESIS_PCT : 0);
    uint32_t last_ms   = p->points[p->num_points - 1].t_ms;

    switch (st->state) {
        case CE_IDLE:
            if (input >= on_level) {
                st->state = CE_PRESSED;
            }
            break;

        case CE_PRESSED:
            if (input < off_level) {
                st->state = CE_RELEASING;
                st->elapsed_ms = 0;
            }
            break;

        case CE_RELEASING:
            st->elapsed_ms += TICK_MS;
            if (input >= on_level) {
                /* Re-engage during release */
                st->state = CE_PRESSED;
            } else if (st->elapsed_ms >= last_ms) {
                st->state = CE_IDLE;
            }
            break;
    }

    switch (st->state) {
        case CE_PRESSED:   return 65535;
        case CE_RELEASING: return profile_value(p, st->elapsed_ms);
        default:           return 0;
    }
}

void clutch_engine_init(const device_config_t *cfg)
{
    taskENTER_CRITICAL(&s_mux);
    s_cfg = *cfg;
    for (int i = 0; i < PADDLE_COUNT; i++) {
        s_status[i] = (clutch_status_t){ .state = CE_IDLE, .elapsed_ms = 0 };
        g_paddle_output[i] = 0;
    }
    taskEXIT_CRITICAL(&s_mux);

    ESP_LOGI(TAG, "Output engine initialized");
}

void clutch_engine_reload_config(const device_config_t *cfg)
{
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < PADDLE_COUNT; i++) {
        if (cfg->paddle[i].mode != s_cfg.paddle[i].mode) {
            s_status[i] = (clutch_status_t){ .state = CE_IDLE, .elapsed_ms = 0 };
        }
    }
    s_cfg = *cfg;
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Config reloaded");
}

clutch_status_t clutch_engine_get_status(int paddle)
{
    clutch_status_t st = { .state = CE_IDLE, .elapsed_ms = 0 };
    if (paddle < 0 || paddle >= PADDLE_COUNT) return st;
    taskENTER_CRITICAL(&s_mux);
    st = s_status[paddle];
    taskEXIT_CRITICAL(&s_mux);
    return st;
}

void task_clutch_engine(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));

        /* Run under the lock so a config reload never tears mid-update */
        taskENTER_CRITICAL(&s_mux);
        for (int i = 0; i < PADDLE_COUNT; i++) {
            const paddle_config_t *p = &s_cfg.paddle[i];
            uint16_t input = g_paddle_input[i];

            if (p->mode == PADDLE_MODE_RELEASE) {
                g_paddle_output[i] = update_release(p, &s_status[i], input);
            } else {
                s_status[i].state = CE_IDLE;
                g_paddle_output[i] = input;
            }
        }
        taskEXIT_CRITICAL(&s_mux);
    }
}
