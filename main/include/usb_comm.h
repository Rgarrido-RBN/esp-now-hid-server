/**
 * @file usb_comm.h
 * @brief USB HID communication handler for PC connection
 * 
 * This module handles USB HID communication with the Windows PC,
 * presenting the device as a game controller/joystick.
 */

#ifndef USB_COMM_H
#define USB_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HID gamepad report structure - 2-axis joystick for clutch paddles
 * 
 * This structure represents the data sent to PC as HID report
 * Optimized for racing simulators (iRacing, Assetto Corsa, etc.)
 */
typedef struct {
    uint16_t left_clutch;       ///< Left clutch paddle - Axis X (0-4095, 12-bit)
    uint16_t right_clutch;      ///< Right clutch paddle - Axis Y (0-4095, 12-bit)
} __attribute__((packed)) usb_hid_gamepad_report_t;

/**
 * @brief Initialize USB HID
 * 
 * Sets up USB HID for game controller communication with PC
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t usb_comm_init(void);

/**
 * @brief Send HID report to PC
 * 
 * @param left_clutch Left clutch value (0-4095, 12-bit ADC)
 * @param right_clutch Right clutch value (0-4095, 12-bit ADC)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t usb_comm_send_report(uint16_t left_clutch, uint16_t right_clutch);

/**
 * @brief Check if USB HID is ready
 * 
 * @return true if USB is connected and ready, false otherwise
 */
bool usb_comm_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // USB_COMM_H
