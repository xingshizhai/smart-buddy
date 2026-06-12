#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Connect to the Wi-Fi network configured via CONFIG_SB_WIFI_SSID /
 * CONFIG_SB_WIFI_PASSWORD. Connection happens asynchronously; this
 * function only starts the driver and registers the auto-reconnect
 * event handlers. */
esp_err_t wifi_manager_start(void);

#ifdef __cplusplus
}
#endif
