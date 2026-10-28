/**
 * @file data_processor.c
 * @brief Data processor implementation for HID gamepad
 */

#include "data_processor.h"
#include "shared_state.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DATA_PROCESSOR";

static bool s_is_initialized = false;
static uint32_t s_total_packets = 0;
static uint32_t s_total_bytes = 0;

/* Shared globals - defined here, declared extern in shared_state.h */
volatile uint16_t g_paddle_input[PADDLE_COUNT] = {0};

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

/*
 * Boot-time auto-calibration: each paddle calibrates itself on its first press.
 * The rest position is averaged from the first samples, then the extreme value
 * reached during the first press/release cycle becomes the fully-pressed value.
 * The paddle may increase or decrease when pressed (rest > full is allowed).
 */
#define AUTOCAL_REST_SAMPLES  16    // samples averaged for the rest position
#define AUTOCAL_PRESS_DELTA   400   // counts from rest to consider the paddle pressed
#define AUTOCAL_DEADZONE      40    // counts ignored at each end of the travel

typedef enum {
    AUTOCAL_SAMPLING_REST,
    AUTOCAL_WAIT_PRESS,
    AUTOCAL_PRESSING,
    AUTOCAL_DONE
} autocal_state_t;

typedef struct {
    autocal_state_t state;
    uint32_t rest_sum;
    uint8_t  rest_count;
    uint16_t rest;
    uint16_t peak;
} paddle_autocal_t;

static paddle_autocal_t s_autocal_left;
static paddle_autocal_t s_autocal_right;

/* Response curve exponents (see data_processor_set_curves) */
static volatile float s_left_curve = 1.0f;
static volatile float s_right_curve = 1.0f;

/* Applies the response curve to a normalized 0-4095 value. */
static uint16_t apply_curve(uint16_t value, float exponent)
{
    if (exponent == 1.0f || value == 0 || value >= 4095) return value;
    return (uint16_t)(powf((float)value / 4095.0f, exponent) * 4095.0f + 0.5f);
}

/* Feeds one sample. Returns true once (when the paddle finishes calibrating). */
static bool autocal_update(paddle_autocal_t *ac, uint16_t raw, const char *name)
{
    int delta = abs((int)raw - (int)ac->rest);

    switch (ac->state) {
        case AUTOCAL_SAMPLING_REST:
            ac->rest_sum += raw;
            if (++ac->rest_count >= AUTOCAL_REST_SAMPLES) {
                ac->rest = (uint16_t)(ac->rest_sum / ac->rest_count);
                ac->state = AUTOCAL_WAIT_PRESS;
                ESP_LOGI(TAG, "%s paddle rest = %d, press it fully once to calibrate", name, ac->rest);
            }
            break;
        case AUTOCAL_WAIT_PRESS:
            if (delta > AUTOCAL_PRESS_DELTA) {
                ac->peak = raw;
                ac->state = AUTOCAL_PRESSING;
            }
            break;
        case AUTOCAL_PRESSING:
            if (delta > abs((int)ac->peak - (int)ac->rest)) {
                ac->peak = raw;
            }
            if (delta < AUTOCAL_PRESS_DELTA / 2) {
                ac->state = AUTOCAL_DONE;
                ESP_LOGI(TAG, "%s paddle calibrated: rest %d -> full %d", name, ac->rest, ac->peak);
                return true;
            }
            break;
        case AUTOCAL_DONE:
            break;
    }
    return false;
}

static void autocal_reset(void)
{
    memset(&s_autocal_left, 0, sizeof(s_autocal_left));
    memset(&s_autocal_right, 0, sizeof(s_autocal_right));
}

/* Maps raw from [rest, full] to [0, 4095]; works for either direction. */
static uint16_t normalize_axis(uint16_t raw, uint16_t rest, uint16_t full)
{
    int span = (int)full - (int)rest;
    int travel = (int)raw - (int)rest;
    if (span < 0) {
        span = -span;
        travel = -travel;
    }
    span -= 2 * AUTOCAL_DEADZONE;
    travel -= AUTOCAL_DEADZONE;
    if (span <= 0 || travel <= 0) return 0;
    if (travel >= span) return 4095;
    return (uint16_t)(((uint32_t)travel * 4095u) / (uint32_t)span);
}

esp_err_t data_processor_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Data processor already initialized");
        return ESP_OK;
    }

    s_total_packets = 0;
    s_total_bytes = 0;
    autocal_reset();
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
    
    // Boot-time auto-calibration (first press of each paddle)
    if (!s_is_calibrating) {
        if (autocal_update(&s_autocal_left, left_clutch_raw, "Left")) {
            s_calibration.left_min = s_autocal_left.rest;
            s_calibration.left_max = s_autocal_left.peak;
        }
        if (autocal_update(&s_autocal_right, right_clutch_raw, "Right")) {
            s_calibration.right_min = s_autocal_right.rest;
            s_calibration.right_max = s_autocal_right.peak;
        }
        if (s_autocal_left.state == AUTOCAL_DONE && s_autocal_right.state == AUTOCAL_DONE) {
            s_calibration.calibrated = true;
        }
    }

    // Normalize values; an uncalibrated paddle outputs 0 so its first
    // (calibration) press does not reach the PC or the clutch engine
    uint16_t left_clutch = 0;
    uint16_t right_clutch = 0;

    if (s_autocal_left.state == AUTOCAL_DONE || s_calibration.calibrated) {
        left_clutch = normalize_axis(left_clutch_raw, s_calibration.left_min, s_calibration.left_max);
    }
    if (s_autocal_right.state == AUTOCAL_DONE || s_calibration.calibrated) {
        right_clutch = normalize_axis(right_clutch_raw, s_calibration.right_min, s_calibration.right_max);
    }

    left_clutch = apply_curve(left_clutch, s_left_curve);
    right_clutch = apply_curve(right_clutch, s_right_curve);

    // Scale both paddles from 12-bit (0-4095) to 16-bit (0-65535)
    g_paddle_input[0] = (uint16_t)(((uint32_t)left_clutch * 65535u) / 4095u);
    g_paddle_input[1] = (uint16_t)(((uint32_t)right_clutch * 65535u) / 4095u);

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

void data_processor_set_curves(uint8_t left_x10, uint8_t right_x10)
{
    s_left_curve = left_x10 / 10.0f;
    s_right_curve = right_x10 / 10.0f;
    ESP_LOGI(TAG, "Response curves: left ^%.1f, right ^%.1f", s_left_curve, s_right_curve);
}

esp_err_t data_processor_reset_calibration(void)
{
    s_calibration.left_min = 0;
    s_calibration.left_max = 4095;
    s_calibration.right_min = 0;
    s_calibration.right_max = 4095;
    s_calibration.calibrated = false;
    s_is_calibrating = false;
    autocal_reset();
    
    ESP_LOGI(TAG, "Calibration reset, waiting for first press of each paddle");
    return ESP_OK;
}
