/**
 * @file main.c
 * @brief Main application entry point — ESP32-S3 Virtual Clutch HID
 *
 * Initialization order (critical):
 *  1. espnow_handler_init()   — NVS flash + WiFi (APSTA mode) + ESP-NOW
 *  2. config_manager_init()   — NVS namespace ready
 *  3. config_manager_load()   — populate g_config
 *  4. usb_comm_init()         — TinyUSB HID device
 *  5. clutch_engine_init()    — state machine
 *  6. web_config_init()       — WiFi AP config + HTTP server
 *  7. xTaskCreatePinnedToCore — spawn three tasks
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"

#include "espnow_handler.h"
#include "usb_comm.h"
#include "data_processor.h"
#include "config_manager.h"
#include "clutch_engine.h"
#include "web_config.h"

static const char *TAG = "MAIN";

/* Live config — shared pointer handed to web_config and clutch_engine */
static clutch_config_t g_config;

/* ESP-NOW data callback (called from WiFi task context) */
static void on_espnow_data_received(const uint8_t *mac_addr,
                                    const uint8_t *data, int len)
{
    esp_err_t ret = data_processor_process_espnow_data(mac_addr, data, len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "data_processor error: %s", esp_err_to_name(ret));
    }
}

/* Periodic status log task */
static void status_task(void *arg)
{
    uint32_t total_packets, total_bytes;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        data_processor_get_stats(&total_packets, &total_bytes);
        ESP_LOGI(TAG, "=== Status === ESP-NOW:%s HID:%s pkts:%lu bytes:%lu heap:%lu",
                 espnow_handler_is_initialized() ? "UP" : "DOWN",
                 usb_comm_is_connected()         ? "UP" : "DOWN",
                 total_packets, total_bytes,
                 esp_get_free_heap_size());
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Virtual Clutch HID ===");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);

    /* 1. WiFi (APSTA mode) + ESP-NOW init — callback NOT registered yet */
    ESP_ERROR_CHECK(espnow_handler_init());

    /* 2. All consumers must be ready before the first packet can arrive */
    ESP_ERROR_CHECK(data_processor_init());

    /* 3. Config (NVS already initialized by espnow_handler_init) */
    ESP_ERROR_CHECK(config_manager_init());
    config_manager_load(&g_config);

    /* 4. USB HID */
    ESP_ERROR_CHECK(usb_comm_init());

    /* 5. Virtual clutch state machine */
    clutch_engine_init(&g_config);

    /* 6. WiFi AP + HTTP server */
    ESP_ERROR_CHECK(web_config_init(&g_config));

    /* 7. FreeRTOS tasks */
    xTaskCreatePinnedToCore(task_clutch_engine, "clutch", 4096,
                            NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(task_hid_reporter,  "hid",    4096,
                            NULL,  8, NULL, 1);
    xTaskCreate(status_task, "status", 3072, NULL, 3, NULL);

    /* 8. Open the gate — register ESP-NOW callback last, once everything is ready */
    ESP_ERROR_CHECK(espnow_handler_register_recv_callback(on_espnow_data_received));

    /* Print MAC for reference */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "WiFi AP: SSID=VirtualClutch  IP=192.168.4.1");
    ESP_LOGI(TAG, "Ready — waiting for ESP-NOW data");
}
