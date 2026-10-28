#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  bite_point;    // 5-80 (%), default 40
    uint16_t bite_hold_ms;  // 50-800 ms, default 150
    uint16_t release_ms;    // 100-2000 ms, default 350
    uint8_t  release_curve; // 0=linear 1=smooth 2=aggressive, default 1
    uint8_t  left_curve;    // 10-40 = response exponent x10 for the left paddle, default 20
    uint8_t  right_curve;   // 10-40 = response exponent x10 for the right paddle, default 20
} clutch_config_t;

/* Initialize config manager. NVS must already be initialized. */
esp_err_t config_manager_init(void);

/* Load config from NVS. Fills *cfg with defaults if keys are missing. */
void config_manager_load(clutch_config_t *cfg);

/* Persist config to NVS. */
void config_manager_save(const clutch_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
