#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    BTN_ACTION_CENTER_SHORT,   /* BOOT short-press: PTT / menu select */
    BTN_ACTION_CENTER_LONG,    /* BOOT long-press: menu / simulate turn */
    BTN_ACTION_LEFT_SHORT,     /* LEFT short-press: mute / back */
    BTN_ACTION_LEFT_LONG,      /* LEFT long-press: pet stats / settings */
} btn_action_t;

esp_err_t ui_button_router_init(void);
void      ui_button_router_handle(btn_action_t action);

/* Context-sensitive helpers */
void ui_button_router_register_center_short_cb(void (*cb)(void *ctx), void *ctx);
void ui_button_router_register_left_short_cb(void (*cb)(void *ctx), void *ctx);
