#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "buddy_hal/hal_display.h"
#include "buddy_hal/hal.h"

#define TAG "DISP"

static hal_display_t s_display;

static esp_err_t display_backlight_set(hal_display_t *disp, uint8_t percent)
{
    bsp_display_brightness_set(percent);
    return ESP_OK;
}

esp_err_t hal_display_create(const hal_display_cfg_t *cfg, hal_display_t **out)
{
    bsp_display_cfg_t bsp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size   = cfg ? cfg->buf_size_px : (BSP_LCD_H_RES * 50),
        .double_buffer = cfg ? cfg->double_buffered : true,
        .flags = {
            .buff_dma    = true,
            .buff_spiram = false,
        },
    };
    bsp_display_start_with_config(&bsp_cfg);
    bsp_display_backlight_on();

    s_display.backlight_set = display_backlight_set;
    *out = &s_display;
    ESP_LOGI(TAG, "display ready %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

void hal_display_destroy(hal_display_t *disp) { }
