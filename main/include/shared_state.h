#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Left clutch paddle pressed state.
 * Written by data_processor (ESP-NOW callback), read by task_clutch_engine.
 * Defined in clutch_engine.c */
extern volatile bool g_left_clutch_pressed;

/* Virtual clutch axis output (0-65535).
 * Written by task_clutch_engine, read by task_hid_reporter.
 * Defined in clutch_engine.c */
extern volatile uint16_t g_virtual_clutch_value;

/* Left paddle axis value (0-65535, already scaled from 12-bit ADC).
 * Written by data_processor (ESP-NOW callback), read by task_hid_reporter.
 * Defined in usb_comm.c */
extern volatile uint16_t g_left_clutch_value;

/* Right clutch axis value (0-65535, already scaled from 12-bit ADC).
 * Written by data_processor (ESP-NOW callback), read by task_hid_reporter.
 * Defined in usb_comm.c */
extern volatile uint16_t g_right_clutch_value;

#ifdef __cplusplus
}
#endif

#endif // SHARED_STATE_H
