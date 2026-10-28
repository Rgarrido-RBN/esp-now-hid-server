#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include "config_manager.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure WiFi AP (SSID "VirtualClutch", pass "iracing123", channel 1)
 * and start the HTTP server.  Call AFTER espnow_handler_init() and
 * clutch_engine_init(), but BEFORE spawning FreeRTOS tasks.
 *
 * @param cfg  Pointer to the live config struct (used for GET /config and
 *             written on POST /config).  Must remain valid for the lifetime
 *             of the application.
 */
esp_err_t web_config_init(clutch_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif // WEB_CONFIG_H
