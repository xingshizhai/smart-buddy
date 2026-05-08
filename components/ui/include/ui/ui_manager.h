#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "state_machine.h"

typedef enum {
    UI_SCREEN_BOOT     = 0,
    UI_SCREEN_MAIN,
    UI_SCREEN_APPROVAL,
    UI_SCREEN_STATUS,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_DEBUG,
    UI_SCREEN_BLE_DEBUG,
    UI_SCREEN_MAX,
} ui_screen_id_t;

typedef enum {
    UI_ANIM_NONE       = 0,
    UI_ANIM_FADE,
    UI_ANIM_SLIDE_LEFT,
    UI_ANIM_SLIDE_RIGHT,
} ui_anim_type_t;

esp_err_t ui_manager_init(void);
void      ui_manager_deinit(void);

esp_err_t      ui_manager_show(ui_screen_id_t id, ui_anim_type_t anim);
esp_err_t      ui_manager_push(ui_screen_id_t id, ui_anim_type_t anim);
esp_err_t      ui_manager_pop(ui_anim_type_t anim);
ui_screen_id_t ui_manager_current(void);

void ui_manager_on_state_change(sm_state_t new_state, sm_state_t old_state, void *ctx);

/* Per-screen update APIs (thread-safe, acquire LVGL lock internally) */
void ui_screen_main_set_state(sm_state_t state);
void ui_screen_main_set_token_count(uint32_t tokens);
void ui_screen_main_set_ble_connected(bool connected);
void ui_screen_main_set_msg(const char *msg);
void ui_screen_main_set_entries(const char (*entries)[92], uint8_t n);
void ui_screen_main_set_passkey(uint32_t passkey);
void ui_screen_approval_set_prompt(const char *tool, const char *hint, const char *id);
void ui_screen_approval_resolve(bool approved);
void ui_approval_handle_key(bool approved);  /* A=approve, B=deny from physical buttons */
void ui_screen_status_refresh(void);
void ui_statusbar_update(void);
