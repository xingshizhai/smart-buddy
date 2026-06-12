#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the on-demand LVGL screenshot TCP server (no-op if
 * CONFIG_DEBUG_SCREENSHOT_ENABLED is not set). The server listens on
 * CONFIG_DEBUG_SCREENSHOT_PORT and, on receiving a single 'S' byte,
 * captures the active LVGL screen via lv_snapshot and sends it back as
 * a small header followed by raw RGB565 pixel data. */
esp_err_t debug_screenshot_start(void);

#ifdef __cplusplus
}
#endif
