#include "usb_comm.h"
#include <string.h>
#include "esp_log.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "tusb.h"

static const char *TAG = "USB_HID";
static bool s_is_mounted = false;
static usb_hid_gamepad_report_t s_report = {0};

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL
};

// HID Report Descriptor - Gamepad with 2 independent axes
// Using X and Y axes for left/right clutch paddles
// Using unsigned 16-bit values (0-65535) for full range compatibility
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    
    // Left Clutch Paddle as X axis
    0x09, 0x30,        //   Usage (X)
    0x15, 0x00,        //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  //   Logical Maximum (65535)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Right Clutch Paddle as Y axis
    0x09, 0x31,        //   Usage (Y)
    0x15, 0x00,        //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  //   Logical Maximum (65535)
    0x75, 0x10,        //   Report Size (16 bits)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    0xC0               // End Collection (Application)
};

// USB Configuration Descriptor
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, 
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, 
                       sizeof(hid_report_descriptor), 0x81, 16, 10)
};

void tud_mount_cb(void) {
    s_is_mounted = true;
    ESP_LOGI(TAG, "USB mounted - Game controller ready!");
}

void tud_umount_cb(void) {
    s_is_mounted = false;
}

void tud_suspend_cb(bool remote_wakeup_en) {}
void tud_resume_cb(void) {}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, 
                                hid_report_type_t report_type, 
                                uint8_t* buffer, uint16_t reqlen) {
    if (reqlen < sizeof(s_report)) return 0;
    memcpy(buffer, &s_report, sizeof(s_report));
    return sizeof(s_report);
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, 
                           hid_report_type_t report_type, 
                           uint8_t const* buffer, uint16_t bufsize) {}

// This callback returns the HID report descriptor (like Arduino does)
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return hid_report_descriptor;
}

esp_err_t usb_comm_init(void) {
    ESP_LOGI(TAG, "Initializing USB HID...");
    
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,  // Use defaults from Kconfig
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    memset(&s_report, 0, sizeof(s_report));
    ESP_LOGI(TAG, "USB HID ready - 2-axis joystick (clutch paddles)");
    return ESP_OK;
}

esp_err_t usb_comm_send_report(uint16_t left_clutch, uint16_t right_clutch) {
    if (!s_is_mounted) return ESP_ERR_INVALID_STATE;
    
    // Convert from 0-4095 (12-bit ADC) to 0-65535 (16-bit for full axis range)
    // Scale up by 16x: multiply by 65535/4095 = ~16
    uint16_t left_scaled = ((uint32_t)left_clutch * 65535) / 4095;
    uint16_t right_scaled = ((uint32_t)right_clutch * 65535) / 4095;
    
    s_report.left_clutch = left_scaled;
    s_report.right_clutch = right_scaled;

    if (tud_hid_ready()) {
        tud_hid_report(0, &s_report, sizeof(s_report));
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

bool usb_comm_is_connected(void) {
    return s_is_mounted;
}
