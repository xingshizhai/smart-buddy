#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "buddy_hal/hal_button.h"
#include "buddy_hal/hal.h"

#define TAG "BTN"

typedef struct {
    button_handle_t handles[HAL_BTN_MAX];
    hal_button_cb_t cbs[HAL_BTN_MAX][4];
    void           *cb_ctxs[HAL_BTN_MAX][4];
} btn_priv_t;

static hal_buttons_t s_buttons;
static btn_priv_t    s_priv;

typedef struct {
    hal_button_id_t    id;
    hal_button_event_t event;
} btn_arg_t;

static btn_arg_t s_args[HAL_BTN_MAX][4];

static void btn_event_dispatch(void *handle, void *arg)
{
    btn_arg_t *a = (btn_arg_t *)arg;
    if (s_buttons.priv) {
        btn_priv_t *p = (btn_priv_t *)s_buttons.priv;
        hal_button_cb_t cb = p->cbs[a->id][a->event];
        if (cb) cb(a->id, a->event, p->cb_ctxs[a->id][a->event]);
    }
}

static esp_err_t btn_register_cb(hal_buttons_t *btns,
                                   hal_button_id_t id,
                                   hal_button_event_t event,
                                   hal_button_cb_t cb,
                                   void *ctx)
{
    btn_priv_t *p = (btn_priv_t *)btns->priv;
    p->cbs[id][event]     = cb;
    p->cb_ctxs[id][event] = ctx;

    button_event_t iot_evt = event == HAL_BTN_EVT_PRESS_DOWN  ? BUTTON_PRESS_DOWN  :
                              event == HAL_BTN_EVT_PRESS_UP    ? BUTTON_PRESS_UP    :
                              event == HAL_BTN_EVT_LONG_PRESS  ? BUTTON_LONG_PRESS_START :
                                                                  BUTTON_DOUBLE_CLICK;
    s_args[id][event].id    = id;
    s_args[id][event].event = event;
    iot_button_register_cb(p->handles[id], iot_evt, NULL, btn_event_dispatch, &s_args[id][event]);
    return ESP_OK;
}

static bool btn_is_pressed(hal_buttons_t *btns, hal_button_id_t id)
{
    btn_priv_t *p = (btn_priv_t *)btns->priv;
    return iot_button_get_key_level(p->handles[id]) == 1;
}

esp_err_t hal_buttons_create(hal_buttons_t **out)
{
    button_handle_t btns[BSP_BUTTON_NUM];
    bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    for (int i = 0; i < HAL_BTN_MAX && i < BSP_BUTTON_NUM; i++) {
        s_priv.handles[i] = btns[i];
    }
    s_buttons.register_cb = btn_register_cb;
    s_buttons.is_pressed  = btn_is_pressed;
    s_buttons.priv        = &s_priv;
    *out = &s_buttons;
    ESP_LOGI(TAG, "buttons ready");
    return ESP_OK;
}

void hal_buttons_destroy(hal_buttons_t *btns) { }
