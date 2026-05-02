#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "buddy_hal/hal.h"
#include "buddy_hal/agent_events.h"
#include "agent_core.h"
#include "agent_stats.h"
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
/* Defined in ui_screen_debug.c */
lv_obj_t *screen_debug_create(void);
void      ui_screen_debug_on_show(void);
void      ui_screen_debug_on_hide(void);

/* Defined in ui_screen_ble_debug.c */
lv_obj_t *screen_ble_debug_create(void);
void      ui_screen_ble_debug_on_show(void);
void      ui_screen_ble_debug_on_hide(void);

#define MAIN_ENTRY_LINES  8
#define MAIN_STATUS_H    28
#define MAIN_MSG_H       22
#define MAIN_ENTRY_H     23
#define MAIN_TRANSCRIPT_Y (MAIN_STATUS_H + MAIN_MSG_H + 2)  /* 52 */

/* Main screen state label, token label, and BLE indicator (updated externally) */
static lv_obj_t *s_main_state_label  = NULL;
static lv_obj_t *s_main_token_label  = NULL;
static lv_obj_t *s_ble_indicator     = NULL;
static lv_obj_t *s_main_msg_label    = NULL;
static lv_obj_t *s_passkey_label     = NULL;
static lv_obj_t *s_entry_labels[MAIN_ENTRY_LINES] = {0};
static lv_obj_t *s_approval_tool     = NULL;
static lv_obj_t *s_approval_hint     = NULL;
static char       s_approval_id_store[64];
static lv_obj_t *s_approval_arc      = NULL;
static lv_timer_t *s_arc_timer       = NULL;
static uint32_t   s_arc_timeout_ms   = 0;
static uint32_t   s_arc_elapsed_ms   = 0;

/* Status screen live labels */
static lv_obj_t *s_status_tokens     = NULL;
static lv_obj_t *s_status_sessions   = NULL;
static lv_obj_t *s_status_approvals  = NULL;
static lv_obj_t *s_status_heap       = NULL;
static lv_obj_t *s_status_transport  = NULL;

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

static void arc_tick_cb(lv_timer_t *t)
{
    s_arc_elapsed_ms += 250;
    if (s_arc_timeout_ms > 0 && s_approval_arc) {
        int32_t pct = 100 - (int32_t)(s_arc_elapsed_ms * 100 / s_arc_timeout_ms);
        if (pct < 0) pct = 0;
        lv_arc_set_value(s_approval_arc, (int16_t)pct);
    }
}

static void approval_timeout_cb(lv_timer_t *t)
{
    if (s_arc_timer) { lv_timer_del(s_arc_timer); s_arc_timer = NULL; }
    if (s_approval_arc) lv_arc_set_value(s_approval_arc, 0);

    /* Auto-deny */
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = false,
        .timestamp_us = 0,
    };
    strlcpy(evt.data.approval_resp.id, s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    lv_timer_del(s_approval_timer);
    s_approval_timer = NULL;
}

static void stop_approval_timers(void)
{
    if (s_arc_timer)      { lv_timer_del(s_arc_timer);      s_arc_timer      = NULL; }
    if (s_approval_timer) { lv_timer_del(s_approval_timer); s_approval_timer = NULL; }
}

static void approve_btn_cb(lv_event_t *e)
{
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = true,
    };
    strlcpy(evt.data.approval_resp.id, s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    stop_approval_timers();
}

static void deny_btn_cb(lv_event_t *e)
{
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = false,
    };
    strlcpy(evt.data.approval_resp.id, s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    stop_approval_timers();
}

static lv_obj_t *screen_boot_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Claude Buddy\nStarting...");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return scr;
}

static void main_settings_btn_cb(lv_event_t *e)
{
    ui_manager_push(UI_SCREEN_SETTINGS, UI_ANIM_SLIDE_LEFT);
}

static lv_obj_t *screen_main_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Status bar ───────────────────────────────────────────────── */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 320, MAIN_STATUS_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x1A, 0x1A, 0x1A), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Settings button */
    lv_obj_t *btn_cfg = lv_btn_create(scr);
    lv_obj_set_size(btn_cfg, 36, MAIN_STATUS_H);
    lv_obj_align(btn_cfg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cfg, lv_color_make(0x38, 0x38, 0x38), 0);
    lv_obj_set_style_bg_color(btn_cfg, lv_color_make(0x60, 0x60, 0x60), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_cfg, 0, 0);
    lv_obj_set_style_shadow_width(btn_cfg, 0, 0);
    lv_obj_set_style_pad_all(btn_cfg, 0, 0);
    lv_obj_add_event_cb(btn_cfg, main_settings_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *icon = lv_label_create(btn_cfg);
    lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(icon, lv_color_make(0xBB, 0xBB, 0xBB), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
    lv_obj_center(icon);

    /* BLE indicator */
    s_ble_indicator = lv_label_create(scr);
    lv_label_set_text(s_ble_indicator, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_ble_indicator, lv_color_make(0x55, 0x55, 0x55), 0);
    lv_obj_set_style_text_font(s_ble_indicator, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ble_indicator, LV_ALIGN_TOP_LEFT, 42, 7);

    /* State label — in status bar, after BLE icon */
    s_main_state_label = lv_label_create(scr);
    lv_label_set_text(s_main_state_label, s_state_labels[SM_STATE_SLEEP]);
    lv_obj_set_style_text_color(s_main_state_label, lv_color_make(0x88, 0x88, 0x88), 0);
    lv_obj_set_style_text_font(s_main_state_label, &lv_font_montserrat_14, 0);
    lv_obj_set_size(s_main_state_label, 140, MAIN_STATUS_H);
    lv_label_set_long_mode(s_main_state_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_main_state_label, LV_ALIGN_TOP_LEFT, 62, 7);

    /* Token counter — right side of status bar */
    s_main_token_label = lv_label_create(scr);
    lv_label_set_text(s_main_token_label, "0 tok");
    lv_obj_set_style_text_color(s_main_token_label, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_set_style_text_font(s_main_token_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_main_token_label, LV_ALIGN_TOP_RIGHT, -6, 7);

    /* ── Thin separator after status bar ──────────────────────────── */
    lv_obj_t *sep1 = lv_obj_create(scr);
    lv_obj_set_size(sep1, 320, 1);
    lv_obj_align(sep1, LV_ALIGN_TOP_LEFT, 0, MAIN_STATUS_H);
    lv_obj_set_style_bg_color(sep1, lv_color_make(0x33, 0x33, 0x33), 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);

    /* ── Msg bar (current status from Claude) ─────────────────────── */
    lv_obj_t *msg_bar = lv_obj_create(scr);
    lv_obj_set_size(msg_bar, 320, MAIN_MSG_H);
    lv_obj_align(msg_bar, LV_ALIGN_TOP_LEFT, 0, MAIN_STATUS_H + 1);
    lv_obj_set_style_bg_color(msg_bar, lv_color_make(0x0D, 0x0D, 0x0D), 0);
    lv_obj_set_style_border_width(msg_bar, 0, 0);
    lv_obj_set_style_radius(msg_bar, 0, 0);
    lv_obj_clear_flag(msg_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_main_msg_label = lv_label_create(scr);
    lv_label_set_text(s_main_msg_label, "");
    lv_obj_set_style_text_color(s_main_msg_label, lv_color_make(0x55, 0x55, 0x55), 0);
    lv_obj_set_style_text_font(s_main_msg_label, &lv_font_montserrat_14, 0);
    lv_obj_set_size(s_main_msg_label, 314, MAIN_MSG_H);
    lv_label_set_long_mode(s_main_msg_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_main_msg_label, LV_ALIGN_TOP_LEFT, 4, MAIN_STATUS_H + 3);

    /* ── Thin separator after msg bar ────────────────────────────── */
    lv_obj_t *sep2 = lv_obj_create(scr);
    lv_obj_set_size(sep2, 320, 1);
    lv_obj_align(sep2, LV_ALIGN_TOP_LEFT, 0, MAIN_STATUS_H + MAIN_MSG_H + 1);
    lv_obj_set_style_bg_color(sep2, lv_color_make(0x22, 0x22, 0x22), 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);

    /* ── Transcript entry lines (newest at top) ───────────────────── */
    /* Brightness fades from white (newest) to dim (oldest) */
    static const uint8_t entry_bright[] = {0xFF, 0xCC, 0xAA, 0x88, 0x66, 0x55, 0x44, 0x44};
    for (int i = 0; i < MAIN_ENTRY_LINES; i++) {
        s_entry_labels[i] = lv_label_create(scr);
        lv_label_set_text(s_entry_labels[i], "");
        uint8_t b = entry_bright[i];
        lv_obj_set_style_text_color(s_entry_labels[i], lv_color_make(b, b, b), 0);
        lv_obj_set_style_text_font(s_entry_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_size(s_entry_labels[i], 314, MAIN_ENTRY_H);
        lv_label_set_long_mode(s_entry_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_align(s_entry_labels[i], LV_ALIGN_TOP_LEFT,
                     4, MAIN_TRANSCRIPT_Y + i * MAIN_ENTRY_H);
    }

    /* ── Passkey overlay (hidden by default, shown during BLE pairing) ─ */
    s_passkey_label = lv_label_create(scr);
    lv_label_set_text(s_passkey_label, "");
    lv_obj_set_style_text_color(s_passkey_label, lv_color_make(0xFF, 0xCC, 0x00), 0);
    lv_obj_set_style_text_font(s_passkey_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(s_passkey_label, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(s_passkey_label, LV_OPA_COVER, 0);
    lv_obj_set_size(s_passkey_label, 320, 240);
    lv_obj_align(s_passkey_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(s_passkey_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);

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

static void status_back_cb(lv_event_t *e) { ui_manager_pop(UI_ANIM_SLIDE_RIGHT); }

static lv_obj_t *make_stat_row(lv_obj_t *parent, int y, const char *icon, const char *init)
{
    lv_obj_t *icon_lbl = lv_label_create(parent);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, lv_color_make(0x88, 0x88, 0x88), 0);
    lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_TOP_LEFT, 10, y);

    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, init);
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 36, y);
    return val;
}

static lv_obj_t *screen_status_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x08, 0x08, 0x10), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Title bar with back button */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 320, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x15, 0x15, 0x25), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_bk = lv_btn_create(bar);
    lv_obj_set_size(btn_bk, 50, 26);
    lv_obj_align(btn_bk, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_bk, lv_color_make(0x30, 0x30, 0x50), 0);
    lv_obj_add_event_cb(btn_bk, status_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bk = lv_label_create(btn_bk);
    lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_bk, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_bk);

    lv_obj_t *lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "Status");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* Stat rows (icon + value) */
    s_status_tokens    = make_stat_row(scr, 44,  LV_SYMBOL_CHARGE,    "-- tokens");
    s_status_sessions  = make_stat_row(scr, 72,  LV_SYMBOL_REFRESH,   "-- sessions");
    s_status_approvals = make_stat_row(scr, 100, LV_SYMBOL_OK,        "--  approved / -- denied");
    s_status_heap      = make_stat_row(scr, 128, LV_SYMBOL_SETTINGS,  "-- KB free");
    s_status_transport = make_stat_row(scr, 156, LV_SYMBOL_WIFI,      "disconnected");

    return scr;
}

static void settings_ble_debug_btn_cb(lv_event_t *e)
{
    ui_manager_push(UI_SCREEN_BLE_DEBUG, UI_ANIM_SLIDE_LEFT);
}

static void settings_debug_btn_cb(lv_event_t *e)
{
    ui_manager_push(UI_SCREEN_DEBUG, UI_ANIM_SLIDE_LEFT);
}

static void settings_status_btn_cb(lv_event_t *e)
{
    ui_screen_status_refresh();
    ui_manager_push(UI_SCREEN_STATUS, UI_ANIM_SLIDE_LEFT);
}

static void settings_back_btn_cb(lv_event_t *e)
{
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

static lv_obj_t *screen_settings_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x0A, 0x0A, 0x12), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Title bar */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 320, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x15, 0x15, 0x25), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);

    lv_obj_t *btn_bk = lv_btn_create(bar);
    lv_obj_set_size(btn_bk, 50, 26);
    lv_obj_align(btn_bk, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_bk, lv_color_make(0x30, 0x30, 0x50), 0);
    lv_obj_add_event_cb(btn_bk, settings_back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bk = lv_label_create(btn_bk);
    lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_bk, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_bk);

    lv_obj_t *lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "Settings");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* Menu list */
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 320, 240 - 36);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_bg_color(list, lv_color_make(0x0A, 0x0A, 0x12), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_row(list, 2, 0);

    lv_obj_t *btn_st = lv_list_add_btn(list, LV_SYMBOL_LIST, "Device Status");
    lv_obj_set_style_bg_color(btn_st, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn_st, lv_color_white(), 0);
    lv_obj_add_event_cb(btn_st, settings_status_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ble = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "BLE Debug");
    lv_obj_set_style_bg_color(btn_ble, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn_ble, lv_color_white(), 0);
    lv_obj_add_event_cb(btn_ble, settings_ble_debug_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_AUDIO, "Audio Debug");
    lv_obj_set_style_bg_color(btn, lv_color_make(0x20, 0x20, 0x35), 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_add_event_cb(btn, settings_debug_btn_cb, LV_EVENT_CLICKED, NULL);

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
    case UI_SCREEN_DEBUG:     return screen_debug_create();
    case UI_SCREEN_BLE_DEBUG: return screen_ble_debug_create();
    default:                 return NULL;
    }
}

static void notify_screen_lifecycle(ui_screen_id_t leaving, ui_screen_id_t entering)
{
    if (leaving == UI_SCREEN_DEBUG)       ui_screen_debug_on_hide();
    if (entering == UI_SCREEN_DEBUG)      ui_screen_debug_on_show();
    if (leaving == UI_SCREEN_BLE_DEBUG)   ui_screen_ble_debug_on_hide();
    if (entering == UI_SCREEN_BLE_DEBUG)  ui_screen_ble_debug_on_show();
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
    ui_screen_id_t prev = ui_manager_current();
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
    notify_screen_lifecycle(prev, id);
    return ESP_OK;
}

esp_err_t ui_manager_push(ui_screen_id_t id, ui_anim_type_t anim)
{
    ui_screen_id_t prev = ui_manager_current();
    if (s_stack_top < SCREEN_STACK_DEPTH - 1) {
        s_stack[++s_stack_top] = id;
    }
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[id],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON      :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT   :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT  :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, id);
    return ESP_OK;
}

esp_err_t ui_manager_pop(ui_anim_type_t anim)
{
    ui_screen_id_t prev = ui_manager_current();
    if (s_stack_top > 0) s_stack_top--;
    ui_screen_id_t next = s_stack[s_stack_top];
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[next],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON      :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT   :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT  :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, next);
    return ESP_OK;
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

void ui_screen_main_set_ble_connected(bool connected)
{
    if (lvgl_port_lock(100)) {
        if (s_ble_indicator) {
            lv_color_t color = connected
                ? lv_color_make(0x00, 0xCC, 0x44)
                : lv_color_make(0x55, 0x55, 0x55);
            lv_obj_set_style_text_color(s_ble_indicator, color, 0);
        }
        lvgl_port_unlock();
    }
}

void ui_screen_main_set_passkey(uint32_t passkey)
{
    if (!lvgl_port_lock(100)) return;
    if (s_passkey_label) {
        if (passkey > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "BLE Pairing\nEnter on desktop:\n\n%06lu", (unsigned long)passkey);
            lv_label_set_text(s_passkey_label, buf);
            lv_obj_clear_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_passkey_label, "");
            lv_obj_add_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lvgl_port_unlock();
}

void ui_screen_main_set_msg(const char *msg)
{
    if (!msg) msg = "";
    if (!lvgl_port_lock(100)) return;
    if (s_main_msg_label) {
        lv_label_set_text(s_main_msg_label, msg);
        /* Amber when there's content, dim grey when empty/ready */
        bool active = msg[0] && strcmp(msg, "ready") != 0;
        lv_color_t c = active
            ? lv_color_make(0xCC, 0x88, 0x00)
            : lv_color_make(0x44, 0x44, 0x44);
        lv_obj_set_style_text_color(s_main_msg_label, c, 0);
    }
    lvgl_port_unlock();
}

void ui_screen_main_set_entries(const char (*entries)[92], uint8_t n)
{
    if (!lvgl_port_lock(100)) return;
    for (int i = 0; i < MAIN_ENTRY_LINES; i++) {
        if (!s_entry_labels[i]) continue;
        if (i < n && entries[i][0])
            lv_label_set_text(s_entry_labels[i], entries[i]);
        else
            lv_label_set_text(s_entry_labels[i], "");
    }
    lvgl_port_unlock();
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
            strlcpy(s_approval_id_store, id, sizeof(s_approval_id_store));
        if (s_approval_arc)
            lv_arc_set_value(s_approval_arc, 100);
        lvgl_port_unlock();
    }

    stop_approval_timers();
    s_arc_timeout_ms = CONFIG_UI_APPROVAL_TIMEOUT_S * 1000;
    s_arc_elapsed_ms = 0;
    s_arc_timer = lv_timer_create(arc_tick_cb, 250, NULL);

    s_approval_timer = lv_timer_create(approval_timeout_cb,
                                        s_arc_timeout_ms, NULL);
    lv_timer_set_repeat_count(s_approval_timer, 1);
}

void ui_screen_status_refresh(void)
{
    agent_stats_t st = agent_stats_get();
    uint32_t free_kb = esp_get_free_heap_size() / 1024;

    /* Determine transport connection string */
    const char *transport_str = "disconnected";
    if (transport_get_state(TRANSPORT_ID_BLE) == TRANSPORT_STATE_CONNECTED)
        transport_str = "BLE connected";
    else if (transport_get_state(TRANSPORT_ID_WS) == TRANSPORT_STATE_CONNECTED)
        transport_str = "WS connected";
    else if (transport_get_state(TRANSPORT_ID_USB) == TRANSPORT_STATE_CONNECTED)
        transport_str = "USB connected";

    if (!lvgl_port_lock(100)) return;

    if (s_status_tokens) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu tokens", (unsigned long)st.tokens_total);
        lv_label_set_text(s_status_tokens, buf);
    }
    if (s_status_sessions) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu sessions", (unsigned long)st.sessions_count);
        lv_label_set_text(s_status_sessions, buf);
    }
    if (s_status_approvals) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%lu approved / %lu denied",
                 (unsigned long)st.approvals_granted, (unsigned long)st.approvals_denied);
        lv_label_set_text(s_status_approvals, buf);
    }
    if (s_status_heap) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu KB free", (unsigned long)free_kb);
        lv_label_set_text(s_status_heap, buf);
    }
    if (s_status_transport) {
        lv_label_set_text(s_status_transport, transport_str);
    }

    lvgl_port_unlock();
}

void ui_statusbar_update(void) { }
