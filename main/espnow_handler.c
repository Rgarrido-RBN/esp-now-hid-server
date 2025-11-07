/**
 * @file espnow_handler.c
 * @brief ESP-NOW communication handler implementation
 */

#include "espnow_handler.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "nvs_flash.h"

static const char *TAG = "ESPNOW_HANDLER";

static bool s_is_initialized = false;
static espnow_recv_callback_t s_recv_callback = NULL;

/**
 * @brief ESP-NOW receive callback (internal)
 * 
 * This function is called by ESP-NOW when data is received
 */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (recv_info == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Invalid receive parameters");
        return;
    }

    ESP_LOGI(TAG, "Received %d bytes from " MACSTR, len, MAC2STR(recv_info->src_addr));

    // Call user callback if registered
    if (s_recv_callback != NULL) {
        s_recv_callback(recv_info->src_addr, data, len);
    }
}

/**
 * @brief ESP-NOW send callback (internal)
 * 
 * This function is called by ESP-NOW when data send completes
 */
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (tx_info == NULL) {
        ESP_LOGE(TAG, "Invalid send callback parameters");
        return;
    }

    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "Send success to " MACSTR, MAC2STR(tx_info->des_addr));
    } else {
        ESP_LOGW(TAG, "Send failed to " MACSTR, MAC2STR(tx_info->des_addr));
    }
}

esp_err_t espnow_handler_init(void)
{
    esp_err_t ret;

    if (s_is_initialized) {
        ESP_LOGW(TAG, "ESP-NOW already initialized");
        return ESP_OK;
    }

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Set WiFi channel (optional, can be configured)
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    // Initialize ESP-NOW
    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    s_is_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW initialized successfully");

    return ESP_OK;
}

esp_err_t espnow_handler_deinit(void)
{
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "ESP-NOW not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_now_deinit();
    s_is_initialized = false;
    s_recv_callback = NULL;

    ESP_LOGI(TAG, "ESP-NOW deinitialized");
    return ESP_OK;
}

esp_err_t espnow_handler_register_recv_callback(espnow_recv_callback_t callback)
{
    if (callback == NULL) {
        ESP_LOGE(TAG, "Invalid callback");
        return ESP_ERR_INVALID_ARG;
    }

    s_recv_callback = callback;
    ESP_LOGI(TAG, "Receive callback registered");
    
    return ESP_OK;
}

esp_err_t espnow_handler_add_peer(const uint8_t *peer_mac)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (peer_mac == NULL) {
        ESP_LOGE(TAG, "Invalid MAC address");
        return ESP_ERR_INVALID_ARG;
    }

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, peer_mac, ESP_NOW_ETH_ALEN);
    peer_info.channel = 1;  // Same as WiFi channel
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;  // No encryption for simplicity

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Peer added: " MACSTR, MAC2STR(peer_mac));
    return ESP_OK;
}

esp_err_t espnow_handler_remove_peer(const uint8_t *peer_mac)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (peer_mac == NULL) {
        ESP_LOGE(TAG, "Invalid MAC address");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_now_del_peer(peer_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove peer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Peer removed: " MACSTR, MAC2STR(peer_mac));
    return ESP_OK;
}

bool espnow_handler_is_initialized(void)
{
    return s_is_initialized;
}
