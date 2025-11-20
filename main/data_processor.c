/**
 * @file data_processor.c
 * @brief Data processor implementation for HID gamepad
 */

#include "data_processor.h"
#include "usb_comm.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DATA_PROCESSOR";

static bool s_is_initialized = false;
static uint32_t s_total_packets = 0;
static uint32_t s_total_bytes = 0;

// Calibration state
static clutch_calibration_t s_calibration = {
    .left_min = 0,
    .left_max = 4095,
    .right_min = 0,
    .right_max = 4095,
    .calibrated = false
};

static bool s_is_calibrating = false;
static int64_t s_calibration_start_time = 0;
static uint32_t s_calibration_duration_ms = 0;
static uint16_t s_calib_left_min = 4095;
static uint16_t s_calib_left_max = 0;
static uint16_t s_calib_right_min = 4095;
static uint16_t s_calib_right_max = 0;

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
    uint16_t left_clutch_raw = (data[1] << 8) | data[0];
    uint16_t right_clutch_raw = (data[3] << 8) | data[2];
    
    // Clamp to 12-bit range (0-4095)
    left_clutch_raw = left_clutch_raw > 4095 ? 4095 : left_clutch_raw;
    right_clutch_raw = right_clutch_raw > 4095 ? 4095 : right_clutch_raw;
    
    // Debug: Log raw values periodically
    static uint32_t log_counter = 0;
    if (log_counter++ % 100 == 0) {
        ESP_LOGI(TAG, "Raw values - Left: %d, Right: %d", left_clutch_raw, right_clutch_raw);
    }

    // If calibrating, update min/max values
    if (s_is_calibrating) {
        if (left_clutch_raw < s_calib_left_min) s_calib_left_min = left_clutch_raw;
        if (left_clutch_raw > s_calib_left_max) s_calib_left_max = left_clutch_raw;
        if (right_clutch_raw < s_calib_right_min) s_calib_right_min = right_clutch_raw;
        if (right_clutch_raw > s_calib_right_max) s_calib_right_max = right_clutch_raw;
        
        // Check if calibration period has ended
        int64_t now = esp_timer_get_time() / 1000; // Convert to ms
        if ((now - s_calibration_start_time) >= s_calibration_duration_ms) {
            // Save calibration values
            s_calibration.left_min = s_calib_left_min;
            s_calibration.left_max = s_calib_left_max;
            s_calibration.right_min = s_calib_right_min;
            s_calibration.right_max = s_calib_right_max;
            s_calibration.calibrated = true;
            s_is_calibrating = false;
            
            ESP_LOGI(TAG, "Calibration complete:");
            ESP_LOGI(TAG, "  Left:  %d - %d", s_calibration.left_min, s_calibration.left_max);
            ESP_LOGI(TAG, "  Right: %d - %d", s_calibration.right_min, s_calibration.right_max);
        }
    }
    
    // Normalize values if calibrated
    uint16_t left_clutch = left_clutch_raw;
    uint16_t right_clutch = right_clutch_raw;
    
    if (s_calibration.calibrated) {
        // Normalize left clutch: map [min, max] -> [0, 4095]
        if (left_clutch_raw <= s_calibration.left_min) {
            left_clutch = 0;
        } else if (left_clutch_raw >= s_calibration.left_max) {
            left_clutch = 4095;
        } else {
            uint32_t range = s_calibration.left_max - s_calibration.left_min;
            if (range > 0) {
                left_clutch = ((uint32_t)(left_clutch_raw - s_calibration.left_min) * 4095) / range;
            }
        }
        
        // Normalize right clutch: map [min, max] -> [0, 4095]
        if (right_clutch_raw <= s_calibration.right_min) {
            right_clutch = 0;
        } else if (right_clutch_raw >= s_calibration.right_max) {
            right_clutch = 4095;
        } else {
            uint32_t range = s_calibration.right_max - s_calibration.right_min;
            if (range > 0) {
                right_clutch = ((uint32_t)(right_clutch_raw - s_calibration.right_min) * 4095) / range;
            }
        }
    }

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

esp_err_t data_processor_start_calibration(uint32_t duration_ms)
{
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_is_calibrating) {
        ESP_LOGW(TAG, "Calibration already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Reset calibration capture values
    s_calib_left_min = 4095;
    s_calib_left_max = 0;
    s_calib_right_min = 4095;
    s_calib_right_max = 0;
    
    s_calibration_duration_ms = duration_ms;
    s_calibration_start_time = esp_timer_get_time() / 1000; // Convert to ms
    s_is_calibrating = true;
    
    ESP_LOGI(TAG, "Starting calibration for %lu ms - move clutches through full range", duration_ms);
    return ESP_OK;
}

esp_err_t data_processor_stop_calibration(void)
{
    if (!s_is_calibrating) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Save current captured values
    s_calibration.left_min = s_calib_left_min;
    s_calibration.left_max = s_calib_left_max;
    s_calibration.right_min = s_calib_right_min;
    s_calibration.right_max = s_calib_right_max;
    s_calibration.calibrated = true;
    s_is_calibrating = false;
    
    ESP_LOGI(TAG, "Calibration stopped manually");
    ESP_LOGI(TAG, "  Left:  %d - %d", s_calibration.left_min, s_calibration.left_max);
    ESP_LOGI(TAG, "  Right: %d - %d", s_calibration.right_min, s_calibration.right_max);
    
    return ESP_OK;
}

bool data_processor_is_calibrating(void)
{
    return s_is_calibrating;
}

esp_err_t data_processor_get_calibration(clutch_calibration_t *calib)
{
    if (calib == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(calib, &s_calibration, sizeof(clutch_calibration_t));
    return ESP_OK;
}

esp_err_t data_processor_set_calibration(const clutch_calibration_t *calib)
{
    if (calib == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_calibration, calib, sizeof(clutch_calibration_t));
    ESP_LOGI(TAG, "Calibration set manually:");
    ESP_LOGI(TAG, "  Left:  %d - %d", s_calibration.left_min, s_calibration.left_max);
    ESP_LOGI(TAG, "  Right: %d - %d", s_calibration.right_min, s_calibration.right_max);
    
    return ESP_OK;
}

esp_err_t data_processor_reset_calibration(void)
{
    s_calibration.left_min = 0;
    s_calibration.left_max = 4095;
    s_calibration.right_min = 0;
    s_calibration.right_max = 4095;
    s_calibration.calibrated = false;
    s_is_calibrating = false;
    
    ESP_LOGI(TAG, "Calibration reset to defaults (no normalization)");
    return ESP_OK;
}
