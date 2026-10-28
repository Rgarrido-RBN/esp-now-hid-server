#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PADDLE_COUNT        2   // 0 = left (X axis), 1 = right (Y axis)
#define RELEASE_MAX_POINTS  8
#define RELEASE_MAX_MS      10000

typedef enum {
    PADDLE_MODE_AXIS    = 0,    // proportional axis (paddle travel -> axis)
    PADDLE_MODE_RELEASE = 1,    // paddle triggers the auto-release profile
} paddle_mode_t;

/* One point of the release profile: output value at t_ms after the paddle is released. */
typedef struct {
    uint16_t t_ms;              // 0-RELEASE_MAX_MS, strictly increasing; first point is always 0
    uint8_t  value;             // 0-100 (%); last point is always 0
} release_point_t;

typedef struct {
    uint8_t  mode;              // paddle_mode_t
    uint8_t  curve_x10;         // 10-40 = response exponent x10 (10 = linear)
    uint8_t  trigger_pct;       // 10-95 (%), travel that triggers auto-release
    uint8_t  num_points;        // 2-RELEASE_MAX_POINTS
    release_point_t points[RELEASE_MAX_POINTS];
} paddle_config_t;

typedef struct {
    paddle_config_t paddle[PADDLE_COUNT];
} device_config_t;

/* Initialize config manager. NVS must already be initialized. */
esp_err_t config_manager_init(void);

/* Fill *cfg with the default config. */
void config_manager_defaults(device_config_t *cfg);

/* Clamp every field of *cfg to its valid range and fix the release profile
 * (sorted times, first point at 0 ms, last point at 0 %). */
void config_manager_sanitize(device_config_t *cfg);

/* Load config from NVS. Fills *cfg with defaults if nothing valid is stored. */
void config_manager_load(device_config_t *cfg);

/* Persist config to NVS. */
void config_manager_save(const device_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
