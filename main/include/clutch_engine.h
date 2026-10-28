#ifndef CLUTCH_ENGINE_H
#define CLUTCH_ENGINE_H

#include <stdint.h>
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Output engine: turns each paddle input into its axis output.
 *  - PADDLE_MODE_AXIS:    output follows the paddle input.
 *  - PADDLE_MODE_RELEASE: output is 100% while the paddle is past its trigger;
 *                         on release it plays the configured release profile.
 */

typedef enum {
    CE_IDLE,
    CE_PRESSED,
    CE_RELEASING,
} clutch_state_t;

typedef struct {
    clutch_state_t state;
    uint32_t       elapsed_ms;  // time since release (valid in CE_RELEASING)
} clutch_status_t;

/* Initialize the engine with a given config. */
void clutch_engine_init(const device_config_t *cfg);

/* Hot-reload config without restarting (called by web server on save). */
void clutch_engine_reload_config(const device_config_t *cfg);

/* Get the current status of one paddle (used by web server /status endpoint). */
clutch_status_t clutch_engine_get_status(int paddle);

/* FreeRTOS task entry point. Pin to core 0, priority 10, stack 4096. */
void task_clutch_engine(void *arg);

#ifdef __cplusplus
}
#endif

#endif // CLUTCH_ENGINE_H
