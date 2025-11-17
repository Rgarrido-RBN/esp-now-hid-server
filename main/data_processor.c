/**
 * @file data_processor.c
 * @brief Data processor implementation for HID gamepad
 */

#include "data_processor.h"
#include "usb_comm.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "DATA_PROCESSOR";

static bool s_is_initialized = false;
static uint32_t s_total_packets = 0;
static uint32_t s_total_bytes = 0;

esp_err_t data_processor_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Data processor already initialized");
        return ESP_OK;
    }

    s_total_packets = 0;
    s_total_bytes = 0;
    s_is_initialized = true;

    ESP_LOGI(TAG, "Data processor initialized successfully");
    return ESP_OK;
}

esp_err_t data_processor_deinit(void)
{
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "Data processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_is_initialized = false;
    ESP_LOGI(TAG, "Data processor deinitialized");
    
    return ESP_OK;
}

esp_err_t data_processor_process_espnow_data(const uint8_t *mac_addr, 
                                             const uint8_t *data, 
                                             int len)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Data processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Update statistics
    s_total_packets++;
    s_total_bytes += len;

    // Expected format from ESP-NOW sender (ESP32-C3 client):
    // Bytes 0-1: left_clutch (uint16_t, little-endian, 12-bit ADC value 0-4095)
    // Bytes 2-3: right_clutch (uint16_t, little-endian, 12-bit ADC value 0-4095)
    
    if (len < 4) {
        ESP_LOGW(TAG, "Packet too small: %d bytes (expected 4)", len);
        return ESP_ERR_INVALID_SIZE;
    }

    // Parse data (little-endian)
    uint16_t left_clutch = (data[1] << 8) | data[0];
    uint16_t right_clutch = (data[3] << 8) | data[2];
    
    // Clamp to 12-bit range (0-4095)
    left_clutch = left_clutch > 4095 ? 4095 : left_clutch;
    right_clutch = right_clutch > 4095 ? 4095 : right_clutch;

    // Send as HID report
    esp_err_t ret = usb_comm_send_report(left_clutch, right_clutch);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to send HID report: %s", esp_err_to_name(ret));
    }

    ESP_LOGD(TAG, "Packet #%lu - Left: %d, Right: %d",
             s_total_packets, left_clutch, right_clutch);

    return ESP_OK;
}

void data_processor_get_stats(uint32_t *total_packets, uint32_t *total_bytes)
{
    if (total_packets != NULL) {
        *total_packets = s_total_packets;
    }
    
    if (total_bytes != NULL) {
        *total_bytes = s_total_bytes;
    }
}
