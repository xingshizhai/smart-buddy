#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port_touch.h"
#include "buddy_hal/hal_touch.h"
#include "buddy_hal/hal.h"

#define TAG "TOUCH"

/* Forward declaration — defined in board_display.c */
lv_display_t *board_display_get_lv_disp(void);

static hal_touch_t s_touch;

static esp_err_t touch_read(hal_touch_t *t, hal_touch_point_t *out)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)t->priv;
    if (!tp) {
        out->pressed = false;
        return ESP_OK;
    }
    uint16_t x, y, strength;
    uint8_t  cnt = 0;
    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, &x, &y, &strength, &cnt, 1);
    out->pressed = pressed && cnt > 0;
    out->x = x;
    out->y = y;
    return ESP_OK;
}

esp_err_t hal_touch_create(hal_touch_t **out)
{
    esp_lcd_touch_handle_t tp = NULL;
    esp_err_t ret = bsp_touch_new(NULL, &tp);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "touch controller not found (err=0x%x) — touch input disabled", ret);
        tp = NULL;
    } else {
        /* Register touch as LVGL input device */
        lv_display_t *disp = board_display_get_lv_disp();
        if (disp) {
            const lvgl_port_touch_cfg_t touch_cfg = {
                .disp   = disp,
                .handle = tp,
            };
            lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
            if (!indev) {
                ESP_LOGW(TAG, "lvgl_port_add_touch failed");
            }
        }
        ESP_LOGI(TAG, "touch ready");
    }

    s_touch.read = touch_read;
    s_touch.priv = tp;
    *out = &s_touch;
    return ESP_OK;
}

void hal_touch_destroy(hal_touch_t *t) { }
