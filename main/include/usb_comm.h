#ifndef USB_COMM_H
#define USB_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HID gamepad report — 2 analog axes (4 bytes):
 *   left_clutch  : X axis  — left paddle (0-65535)
 *   right_clutch : Y axis  — right paddle (0-65535)
 */
typedef struct {
    uint16_t left_clutch;     ///< X axis: left paddle
    uint16_t right_clutch;    ///< Y axis: right paddle
} __attribute__((packed)) usb_hid_gamepad_report_t;

/** Initialize TinyUSB HID device. Call once before spawning tasks. */
esp_err_t usb_comm_init(void);

/** Returns true when the USB HID device is mounted (PC connected). */
bool usb_comm_is_connected(void);

/**
 * FreeRTOS task: reads g_paddle_output (X = left, Y = right)
 * every 10 ms and sends a HID report.
 * Pin to core 1, priority 8, stack 4096.
 */
void task_hid_reporter(void *arg);

#ifdef __cplusplus
}
#endif

#endif // USB_COMM_H
