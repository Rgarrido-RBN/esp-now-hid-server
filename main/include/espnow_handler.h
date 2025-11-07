/**
 * @file espnow_handler.h
 * @brief ESP-NOW communication handler for receiving data
 * 
 * This module handles ESP-NOW initialization, registration of peers,
 * and receiving data from other ESP-NOW devices.
 */

#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum payload size for ESP-NOW packets
 */
#define ESPNOW_MAX_DATA_LEN 250

/**
 * @brief Callback function type for received ESP-NOW data
 * 
 * @param mac_addr MAC address of the sender
 * @param data Pointer to received data
 * @param len Length of received data
 */
typedef void (*espnow_recv_callback_t)(const uint8_t *mac_addr, const uint8_t *data, int len);

/**
 * @brief Initialize ESP-NOW
 * 
 * Initializes WiFi and ESP-NOW protocol
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_handler_init(void);

/**
 * @brief Deinitialize ESP-NOW
 * 
 * Cleans up ESP-NOW resources
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_handler_deinit(void);

/**
 * @brief Register callback for received data
 * 
 * @param callback Function to call when data is received
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_handler_register_recv_callback(espnow_recv_callback_t callback);

/**
 * @brief Add a peer device
 * 
 * @param peer_mac MAC address of the peer device
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_handler_add_peer(const uint8_t *peer_mac);

/**
 * @brief Remove a peer device
 * 
 * @param peer_mac MAC address of the peer device
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t espnow_handler_remove_peer(const uint8_t *peer_mac);

/**
 * @brief Check if ESP-NOW is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool espnow_handler_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_HANDLER_H
