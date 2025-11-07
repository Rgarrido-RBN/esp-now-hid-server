/**
 * @file data_processor.h
 * @brief Data processor for handling ESP-NOW received data
 * 
 * This module processes data received from ESP-NOW and converts it
 * to HID reports for the PC gamepad interface.
 */

#ifndef DATA_PROCESSOR_H
#define DATA_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum size of processed data buffer
 */
#define DATA_PROCESSOR_BUFFER_SIZE 512

/**
 * @brief ESP-NOW data packet structure for sim racing controls
 * 
 * This structure defines the expected format from the ESP-NOW sender
 */
typedef struct {
    uint32_t buttons;           ///< Button states (32 buttons, 1 bit each)
    uint16_t left_clutch;       ///< Left clutch paddle analog value (0-4095 or 0-1023)
    uint16_t right_clutch;      ///< Right clutch paddle analog value (0-4095 or 0-1023)
    uint16_t axis_x;            ///< Additional axis (optional)
    uint16_t axis_y;            ///< Additional axis (optional)
    uint16_t axis_z;            ///< Additional axis (optional)
    uint16_t axis_rx;           ///< Additional axis (optional)
    uint8_t checksum;           ///< Optional checksum for validation
} __attribute__((packed)) espnow_simracing_data_t;

/**
 * @brief Data packet statistics
 */
typedef struct {
    uint8_t sender_mac[6];      ///< MAC address of sender
    uint32_t timestamp;         ///< Timestamp when received (milliseconds)
    int8_t rssi;                ///< Signal strength
} data_packet_info_t;

/**
 * @brief Initialize data processor
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t data_processor_init(void);

/**
 * @brief Deinitialize data processor
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t data_processor_deinit(void);

/**
 * @brief Process received ESP-NOW data and convert to HID report
 * 
 * This function processes raw ESP-NOW data and forwards it as HID gamepad input
 * 
 * @param mac_addr MAC address of sender
 * @param data Pointer to received data
 * @param len Length of received data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t data_processor_process_espnow_data(const uint8_t *mac_addr, 
                                             const uint8_t *data, 
                                             int len);

/**
 * @brief Parse raw ESP-NOW data into sim racing structure
 * 
 * @param raw_data Raw received data
 * @param len Length of raw data
 * @param parsed_data Output parsed structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t data_processor_parse_simracing_data(const uint8_t *raw_data,
                                               int len,
                                               espnow_simracing_data_t *parsed_data);

/**
 * @brief Get statistics about processed data
 * 
 * @param total_packets Pointer to store total packets processed
 * @param total_bytes Pointer to store total bytes processed
 */
void data_processor_get_stats(uint32_t *total_packets, uint32_t *total_bytes);

/**
 * @brief Get last packet info
 * 
 * @param info Pointer to store last packet info
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t data_processor_get_last_packet_info(data_packet_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // DATA_PROCESSOR_H
