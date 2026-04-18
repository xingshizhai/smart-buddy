#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_lcd_touch.h"
#include "buddy_hal/hal_touch.h"
#include "buddy_hal/hal.h"

#define TAG "TOUCH"

static hal_touch_t s_touch;

static esp_err_t touch_read(hal_touch_t *t, hal_touch_point_t *out)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)t->priv;
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
    bsp_touch_new(NULL, &tp);
    s_touch.read  = touch_read;
    s_touch.priv  = tp;
    *out = &s_touch;
    ESP_LOGI(TAG, "touch ready");
    return ESP_OK;
}

void hal_touch_destroy(hal_touch_t *t) { }
