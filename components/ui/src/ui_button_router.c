#include "ui/ui_manager.h"
#include "ui/ui_button_router.h"

static void (*s_center_short_cb)(void *) = NULL;
static void *s_center_short_ctx = NULL;
static void (*s_left_short_cb)(void *) = NULL;
static void *s_left_short_ctx = NULL;

esp_err_t ui_button_router_init(void)
{
    return ESP_OK;
}

void ui_button_router_register_center_short_cb(void (*cb)(void *), void *ctx)
{
    s_center_short_cb = cb;
    s_center_short_ctx = ctx;
}

void ui_button_router_register_left_short_cb(void (*cb)(void *), void *ctx)
{
    s_left_short_cb = cb;
    s_left_short_ctx = ctx;
}

void ui_button_router_handle(btn_action_t action)
{
    ui_screen_id_t cur = ui_manager_current();

    switch (action) {
    case BTN_ACTION_CENTER_SHORT:
        /* On MAIN screen: if a custom callback is set (PTT), use it */
        if (cur == UI_SCREEN_MAIN && s_center_short_cb) {
            s_center_short_cb(s_center_short_ctx);
        }
        break;

    case BTN_ACTION_CENTER_LONG:
        /* On MAIN screen: open menu overlay */
        if (cur == UI_SCREEN_MAIN) {
            ui_manager_push(UI_SCREEN_MENU, UI_ANIM_FADE);
        }
        break;

    case BTN_ACTION_LEFT_SHORT:
        /* Back button on sub-screens, or mute on MAIN */
        if (cur == UI_SCREEN_MAIN && s_left_short_cb) {
            s_left_short_cb(s_left_short_ctx);
        } else if (cur != UI_SCREEN_MAIN && cur != UI_SCREEN_BOOT) {
            ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
        }
        break;

    case BTN_ACTION_LEFT_LONG:
        /* On MAIN screen: open pet stats */
        if (cur == UI_SCREEN_MAIN) {
            ui_manager_push(UI_SCREEN_STATS, UI_ANIM_SLIDE_LEFT);
        }
        break;
    }
}
