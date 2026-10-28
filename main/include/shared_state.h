#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "config_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paddle input (0-65535): calibrated travel with the response curve applied.
 * Index 0 = left, 1 = right.
 * Written by data_processor (ESP-NOW callback), read by task_clutch_engine.
 * Defined in data_processor.c */
extern volatile uint16_t g_paddle_input[PADDLE_COUNT];

/* Axis output sent to the PC (0-65535). Index 0 = X axis, 1 = Y axis.
 * Written by task_clutch_engine, read by task_hid_reporter.
 * Defined in clutch_engine.c */
extern volatile uint16_t g_paddle_output[PADDLE_COUNT];

#ifdef __cplusplus
}
#endif

#endif // SHARED_STATE_H
