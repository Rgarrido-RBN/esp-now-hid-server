/**
 * @file main.c
 * @brief Main application entry point
 * 
 * ESP32-S3 project that receives data via ESP-NOW and presents it
 * to a Windows PC as a USB HID game controller.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "espnow_handler.h"
#include "usb_comm.h"
#include "data_processor.h"

static const char *TAG = "MAIN";

/**
 * @brief Callback for ESP-NOW received data
 * 
 * This function is called when ESP-NOW data is received
 */
static void on_espnow_data_received(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    ESP_LOGD(TAG, "ESP-NOW data received from %02X:%02X:%02X:%02X:%02X:%02X, length: %d", 
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5], len);
    
    // Process the received data and send as HID report
    esp_err_t ret = data_processor_process_espnow_data(mac_addr, data, len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to process ESP-NOW data: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Status monitoring task
 * 
 * Periodically prints system status and statistics
 */
static void status_task(void *arg)
{
    uint32_t total_packets, total_bytes;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds
        
        data_processor_get_stats(&total_packets, &total_bytes);
        
        ESP_LOGI(TAG, "=== System Status ===");
        ESP_LOGI(TAG, "ESP-NOW: %s", 
                 espnow_handler_is_initialized() ? "Running" : "Stopped");
        ESP_LOGI(TAG, "USB HID: %s", 
                 usb_comm_is_connected() ? "Connected" : "Disconnected");
        ESP_LOGI(TAG, "Packets processed: %lu", total_packets);
        ESP_LOGI(TAG, "Total bytes: %lu", total_bytes);
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    }
}

/**
 * @brief Initialize all system modules
 */
static esp_err_t init_system(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing system modules...");
    
    // Initialize USB HID first
    ret = usb_comm_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB HID: %s", 
                 esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "USB HID initialized - Device will appear as game controller");
    
    // Initialize data processor
    ret = data_processor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize data processor: %s", 
                 esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize ESP-NOW
    ret = espnow_handler_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register ESP-NOW callback
    ret = espnow_handler_register_recv_callback(on_espnow_data_received);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ESP-NOW callback: %s", 
                 esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "System initialization complete");
    
    return ESP_OK;
}

/**
 * @brief Main application entry point
 */
/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Clutch Paddles - USB HID Game Controller ===");
    ESP_LOGI(TAG, "Build date: %s %s", __DATE__, __TIME__);
    
    // Initialize system
    esp_err_t ret = init_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed!");
        return;
    }
    
    // Create status monitoring task
    BaseType_t task_ret = xTaskCreate(status_task, "status_task", 
                                      3072, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create status task");
    }
    
    // Print MAC address for reference
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Device MAC address: %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Device ready as USB HID gamepad");
    ESP_LOGI(TAG, "Waiting for ESP-NOW data from sim racing controller...");
}
