#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "buddy_hal/hal.h"
#include "buddy_hal/agent_events.h"
#include "agent_core.h"
#include "transport/transport.h"

#define TAG "UI"

#ifndef CONFIG_UI_APPROVAL_TIMEOUT_S
#define CONFIG_UI_APPROVAL_TIMEOUT_S 30
#endif
#ifndef CONFIG_UI_SCREEN_OFF_TIMEOUT_S
#define CONFIG_UI_SCREEN_OFF_TIMEOUT_S 30
#endif
#define SCREEN_STACK_DEPTH 4

static ui_screen_id_t s_stack[SCREEN_STACK_DEPTH];
static int            s_stack_top = -1;
static lv_obj_t      *s_screens[UI_SCREEN_MAX] = {0};

/* Forward declarations for screen create functions */
static lv_obj_t *screen_boot_create(void);
static lv_obj_t *screen_main_create(void);
static lv_obj_t *screen_approval_create(void);
static lv_obj_t *screen_status_create(void);
static lv_obj_t *screen_settings_create(void);

/* Main screen state label and token label (updated externally) */
static lv_obj_t *s_main_state_label  = NULL;
static lv_obj_t *s_main_token_label  = NULL;
static lv_obj_t *s_approval_tool     = NULL;
static lv_obj_t *s_approval_hint     = NULL;
static lv_obj_t *s_approval_id_store[64];
static lv_obj_t *s_approval_arc      = NULL;

static lv_timer_t *s_approval_timer  = NULL;
static lv_timer_t *s_screenoff_timer = NULL;

static const char *s_state_labels[] = {
    "Sleeping...", "Idle", "Working...", "Needs Approval!",
    "Celebrating!", "Dizzy...", "Happy!"
};

static void screenoff_timer_cb(lv_timer_t *t)
{
    /* Don't turn off during approval */
    if (ui_manager_current() == UI_SCREEN_APPROVAL) return;
    /* Backlight off via HAL */
    extern hal_handles_t g_hal;
    if (g_hal.display)
        g_hal.display->backlight_set(g_hal.display, 0);
}

static void approval_timeout_cb(lv_timer_t *t)
{
    /* Auto-deny */
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = false,
        .timestamp_us = 0,
    };
    /* s_approval_id_store holds the id string */
    strlcpy(evt.data.approval_resp.id, (const char *)s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    lv_timer_del(s_approval_timer);
    s_approval_timer = NULL;
}

static void approve_btn_cb(lv_event_t *e)
{
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = true,
    };
    strlcpy(evt.data.approval_resp.id, (const char *)s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    if (s_approval_timer) { lv_timer_del(s_approval_timer); s_approval_timer = NULL; }
}

static void deny_btn_cb(lv_event_t *e)
{
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = false,
    };
    strlcpy(evt.data.approval_resp.id, (const char *)s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    if (s_approval_timer) { lv_timer_del(s_approval_timer); s_approval_timer = NULL; }
}

static lv_obj_t *screen_boot_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "SmartBuddy\nStarting...");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return scr;
}

static lv_obj_t *screen_main_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    /* Status bar */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 320, 24);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x20, 0x20, 0x20), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 2, 0);

    s_main_token_label = lv_label_create(bar);
    lv_label_set_text(s_main_token_label, "0 tok");
    lv_obj_set_style_text_color(s_main_token_label, lv_color_white(), 0);
    lv_obj_align(s_main_token_label, LV_ALIGN_RIGHT_MID, -4, 0);

    /* Avatar placeholder */
    lv_obj_t *avatar = lv_label_create(scr);
    lv_label_set_text(avatar, "(^_^)");
    lv_obj_set_style_text_color(avatar, lv_color_white(), 0);
    lv_obj_set_style_text_font(avatar, &lv_font_montserrat_24, 0);
    lv_obj_align(avatar, LV_ALIGN_CENTER, 0, -20);

    /* State label */
    s_main_state_label = lv_label_create(scr);
    lv_label_set_text(s_main_state_label, "Idle");
    lv_obj_set_style_text_color(s_main_state_label, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_align(s_main_state_label, LV_ALIGN_CENTER, 0, 40);

    return scr;
}

static lv_obj_t *screen_approval_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x10, 0x10, 0x10), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "APPROVAL NEEDED");
    lv_obj_set_style_text_color(title, lv_color_make(0xFF, 0xA0, 0x00), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_approval_tool = lv_label_create(scr);
    lv_label_set_text(s_approval_tool, "Tool: -");
    lv_obj_set_style_text_color(s_approval_tool, lv_color_white(), 0);
    lv_obj_align(s_approval_tool, LV_ALIGN_TOP_LEFT, 10, 40);

    s_approval_hint = lv_label_create(scr);
    lv_label_set_text(s_approval_hint, "");
    lv_obj_set_width(s_approval_hint, 300);
    lv_label_set_long_mode(s_approval_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_approval_hint, lv_color_make(0xCC, 0xCC, 0xCC), 0);
    lv_obj_align(s_approval_hint, LV_ALIGN_TOP_LEFT, 10, 62);

    /* Approve button */
    lv_obj_t *btn_ok = lv_btn_create(scr);
    lv_obj_set_size(btn_ok, 130, 50);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_LEFT, 10, -30);
    lv_obj_set_style_bg_color(btn_ok, lv_color_make(0x00, 0x99, 0x33), 0);
    lv_obj_add_event_cb(btn_ok, approve_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "APPROVE");
    lv_obj_center(lbl_ok);

    /* Deny button */
    lv_obj_t *btn_no = lv_btn_create(scr);
    lv_obj_set_size(btn_no, 130, 50);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -10, -30);
    lv_obj_set_style_bg_color(btn_no, lv_color_make(0xCC, 0x11, 0x11), 0);
    lv_obj_add_event_cb(btn_no, deny_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "DENY");
    lv_obj_center(lbl_no);

    /* Countdown arc */
    s_approval_arc = lv_arc_create(scr);
    lv_obj_set_size(s_approval_arc, 40, 40);
    lv_arc_set_range(s_approval_arc, 0, 100);
    lv_arc_set_value(s_approval_arc, 100);
    lv_obj_align(s_approval_arc, LV_ALIGN_BOTTOM_MID, 0, -28);

    return scr;
}

static lv_obj_t *screen_status_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Status Screen\n(touch to go back)");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return scr;
}

static lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Settings Screen\n(touch to go back)");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return scr;
}

static lv_obj_t *create_screen(ui_screen_id_t id)
{
    switch (id) {
    case UI_SCREEN_BOOT:     return screen_boot_create();
    case UI_SCREEN_MAIN:     return screen_main_create();
    case UI_SCREEN_APPROVAL: return screen_approval_create();
    case UI_SCREEN_STATUS:   return screen_status_create();
    case UI_SCREEN_SETTINGS: return screen_settings_create();
    default:                 return NULL;
    }
}

esp_err_t ui_manager_init(void)
{
    /* Create all screens upfront */
    for (int i = 0; i < UI_SCREEN_MAX; i++) {
        s_screens[i] = create_screen((ui_screen_id_t)i);
    }
    return ESP_OK;
}

void ui_manager_deinit(void)
{
    for (int i = 0; i < UI_SCREEN_MAX; i++) {
        if (s_screens[i]) lv_obj_del(s_screens[i]);
    }
}

esp_err_t ui_manager_show(ui_screen_id_t id, ui_anim_type_t anim)
{
    if (id >= UI_SCREEN_MAX) return ESP_ERR_INVALID_ARG;
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[id],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON      :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT   :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT  :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        s_stack_top = 0;
        s_stack[0]  = id;
        lvgl_port_unlock();
    }
    return ESP_OK;
}

esp_err_t ui_manager_push(ui_screen_id_t id, ui_anim_type_t anim)
{
    if (s_stack_top < SCREEN_STACK_DEPTH - 1) {
        s_stack[++s_stack_top] = id;
    }
    return ui_manager_show(id, anim);
}

esp_err_t ui_manager_pop(ui_anim_type_t anim)
{
    if (s_stack_top > 0) s_stack_top--;
    return ui_manager_show(s_stack[s_stack_top], anim);
}

ui_screen_id_t ui_manager_current(void)
{
    return s_stack_top >= 0 ? s_stack[s_stack_top] : UI_SCREEN_BOOT;
}

void ui_manager_on_state_change(sm_state_t new_state, sm_state_t old_state, void *ctx)
{
    switch (new_state) {
    case SM_STATE_ATTENTION:
        ui_manager_push(UI_SCREEN_APPROVAL, UI_ANIM_SLIDE_LEFT);
        if (s_screenoff_timer) lv_timer_pause(s_screenoff_timer);
        break;
    case SM_STATE_SLEEP:
        ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_NONE);
        break;
    default:
        if (old_state == SM_STATE_ATTENTION) {
            ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
            if (s_screenoff_timer) lv_timer_resume(s_screenoff_timer);
        }
        ui_screen_main_set_state(new_state);
        break;
    }
}

void ui_screen_main_set_state(sm_state_t state)
{
    if (state >= SM_STATE_MAX) return;
    if (lvgl_port_lock(100)) {
        if (s_main_state_label)
            lv_label_set_text(s_main_state_label, s_state_labels[state]);
        lvgl_port_unlock();
    }
}

void ui_screen_main_set_token_count(uint32_t tokens)
{
    if (lvgl_port_lock(100)) {
        if (s_main_token_label) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lu tok", (unsigned long)tokens);
            lv_label_set_text(s_main_token_label, buf);
        }
        lvgl_port_unlock();
    }
}

void ui_screen_approval_set_prompt(const char *tool, const char *hint, const char *id)
{
    if (lvgl_port_lock(100)) {
        if (s_approval_tool) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Tool: %s", tool ? tool : "?");
            lv_label_set_text(s_approval_tool, buf);
        }
        if (s_approval_hint)
            lv_label_set_text(s_approval_hint, hint ? hint : "");
        if (id)
            strlcpy((char *)s_approval_id_store, id, sizeof(s_approval_id_store));
        if (s_approval_arc)
            lv_arc_set_value(s_approval_arc, 100);
        lvgl_port_unlock();
    }

    if (s_approval_timer) { lv_timer_del(s_approval_timer); s_approval_timer = NULL; }
    s_approval_timer = lv_timer_create(approval_timeout_cb,
                                        CONFIG_UI_APPROVAL_TIMEOUT_S * 1000, NULL);
    lv_timer_set_repeat_count(s_approval_timer, 1);
}

void ui_screen_status_refresh(void) { /* TODO: populate status widgets */ }
void ui_statusbar_update(void)      { /* TODO: refresh connection indicators */ }
