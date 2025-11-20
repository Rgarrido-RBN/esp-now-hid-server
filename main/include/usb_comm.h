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
 * @brief HID gamepad report structure - 1 button + 1 analog axis
 * 
 * This structure represents the data sent to PC as HID report
 * Left clutch = Button 1 (digital)
 * Right clutch = X axis (analog 0-65535)
 */
typedef struct {
    uint8_t buttons;            ///< Button states (bit 0 = left clutch button)
    uint16_t right_clutch;      ///< Right clutch paddle - Axis X (0-65535, 16-bit)
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
