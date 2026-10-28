#ifndef CLUTCH_ENGINE_H
#define CLUTCH_ENGINE_H

#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CE_IDLE,
    CE_PRESSED,
    CE_BITE_HOLD,
    CE_RELEASING,
} clutch_state_t;

/* Initialize the clutch engine with a given config. */
void clutch_engine_init(const clutch_config_t *cfg);

/* Hot-reload config without restarting (called by web server on save). */
void clutch_engine_reload_config(const clutch_config_t *cfg);

/* Get current state (used by web server /status endpoint). */
clutch_state_t clutch_engine_get_state(void);

/* FreeRTOS task entry point. Pin to core 0, priority 10, stack 4096. */
void task_clutch_engine(void *arg);

#ifdef __cplusplus
}
#endif

#endif // CLUTCH_ENGINE_H
