#include "usb_comm.h"
#include "shared_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_log.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "tusb.h"

static const char *TAG = "USB_HID";

static bool s_is_mounted = false;
static usb_hid_gamepad_report_t s_report = {0};

/* g_right_clutch_value: written by data_processor, read by this task */
volatile uint16_t g_right_clutch_value = 0;

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL
};

/*
 * HID Report Descriptor — Gamepad with 2 analog axes (no buttons):
 *   Usage X  (0x30) : right clutch paddle  (0-65535, 16-bit)
 *   Usage Y  (0x31) : virtual clutch axis  (0-65535, 16-bit)
 */
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x05,             // Usage (Game Pad)
    0xA1, 0x01,             // Collection (Application)

    /* X axis — right clutch paddle */
    0x09, 0x30,             //   Usage (X)
    0x15, 0x00,             //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  // Logical Maximum (65535)
    0x75, 0x10,             //   Report Size (16 bits)
    0x95, 0x01,             //   Report Count (1)
    0x81, 0x02,             //   Input (Data, Variable, Absolute)

    /* Y axis — virtual clutch engine */
    0x09, 0x31,             //   Usage (Y)
    0x15, 0x00,             //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  // Logical Maximum (65535)
    0x75, 0x10,             //   Report Size (16 bits)
    0x95, 0x01,             //   Report Count (1)
    0x81, 0x02,             //   Input (Data, Variable, Absolute)

    0xC0                    // End Collection
};

static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(hid_report_descriptor), 0x81, 16, 10)
};

/* ------------------------------------------------------------------ */
/* TinyUSB callbacks                                                   */
/* ------------------------------------------------------------------ */

void tud_mount_cb(void)
{
    s_is_mounted = true;
    ESP_LOGI(TAG, "USB mounted — gamepad ready (X=right clutch, Y=virtual clutch)");
}

void tud_umount_cb(void)       { s_is_mounted = false; }
void tud_suspend_cb(bool rwe)  { (void)rwe; }
void tud_resume_cb(void)       {}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    if (reqlen < sizeof(s_report)) return 0;
    memcpy(buffer, &s_report, sizeof(s_report));
    return sizeof(s_report);
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            const uint8_t *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)bufsize;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return hid_report_descriptor;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t usb_comm_init(void)
{
    ESP_LOGI(TAG, "Initializing USB HID (2-axis gamepad)...");

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor      = NULL,
        .string_descriptor      = NULL,
        .external_phy           = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    memset(&s_report, 0, sizeof(s_report));

    ESP_LOGI(TAG, "USB HID ready");
    return ESP_OK;
}

bool usb_comm_is_connected(void)
{
    return s_is_mounted;
}

void task_hid_reporter(void *arg)
{
    (void)arg;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));

        if (!s_is_mounted) continue;

        s_report.right_clutch   = g_right_clutch_value;
        s_report.virtual_clutch = g_virtual_clutch_value;

        if (tud_hid_ready()) {
            tud_hid_report(0, &s_report, sizeof(s_report));
        }
    }
}
