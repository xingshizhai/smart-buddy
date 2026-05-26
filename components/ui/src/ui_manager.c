#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "ui/persona.h"
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
/* Defined in separate source files */
lv_obj_t *screen_stats_create(void);
lv_obj_t *screen_info_create(void);
lv_obj_t *screen_menu_create(void);
lv_obj_t *screen_clock_create(void);
/* Defined in ui_screen_debug.c */
lv_obj_t *screen_debug_create(void);
void      ui_screen_debug_on_show(void);
void      ui_screen_debug_on_hide(void);

/* Defined in ui_screen_ble_debug.c */
lv_obj_t *screen_ble_debug_create(void);
void      ui_screen_ble_debug_on_show(void);
void      ui_screen_ble_debug_on_hide(void);

#define MAIN_STATUS_H  28

/* Main screen labels and BLE indicator */
static lv_obj_t *s_main_title_label   = NULL;
static lv_obj_t *s_persona_label      = NULL;  /* ASCII art animation */
static lv_obj_t *s_main_content_label = NULL;
static lv_obj_t *s_ble_indicator      = NULL;
static lv_obj_t *s_passkey_label     = NULL;
static lv_obj_t *s_approval_tool     = NULL;
static lv_obj_t *s_approval_hint     = NULL;
static char       s_approval_id_store[64];
static lv_obj_t *s_approval_arc      = NULL;
static lv_timer_t *s_arc_timer       = NULL;
static uint32_t   s_arc_timeout_ms   = 0;
static uint32_t   s_arc_elapsed_ms   = 0;

/* HUD transcript area */
static lv_obj_t *s_transcript_area   = NULL;
static lv_obj_t *s_transcript_label  = NULL;
static bool      s_transcript_visible = true;

/* Screen-off control */
static uint8_t   s_saved_brightness = 100;
static bool      s_screen_dimmed    = false;

/* Status screen live labels */
static lv_obj_t *s_status_tokens     = NULL;
static lv_obj_t *s_status_sessions   = NULL;
static lv_obj_t *s_status_approvals  = NULL;
static lv_obj_t *s_status_heap       = NULL;
static lv_obj_t *s_status_transport  = NULL;

static lv_timer_t *s_approval_timer  = NULL;
static lv_timer_t *s_screenoff_timer = NULL;

/* Persona animation frame callback — called from LVGL timer context */
static void persona_frame_cb(const char *frame, void *ctx)
{
    (void)ctx;
    if (s_persona_label)
        lv_label_set_text(s_persona_label, frame);
}

static void screenoff_timer_cb(lv_timer_t *t)
{
    /* Don't turn off during approval */
    if (ui_manager_current() == UI_SCREEN_APPROVAL) return;

    extern hal_handles_t g_hal;
    if (!g_hal.display) return;

    if (!s_screen_dimmed) {
        /* First timeout: dim to 20% */
        g_hal.display->backlight_set(g_hal.display, 20);
        s_saved_brightness = 100; /* remember previous level */
        s_screen_dimmed = true;
        /* Restart timer for next phase (full off) */
        if (s_screenoff_timer) {
            lv_timer_set_period(s_screenoff_timer,
                (uint32_t)CONFIG_UI_SCREEN_OFF_TIMEOUT_S * 1000);
            lv_timer_set_repeat_count(s_screenoff_timer, 1);
            lv_timer_resume(s_screenoff_timer);
        }
    } else {
        /* Second timeout: turn off completely */
        g_hal.display->backlight_set(g_hal.display, 0);
    }
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

/* Physical button handler — called from main.c when A or B key is pressed */
void ui_approval_handle_key(bool approved)
{
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = approved,
    };
    strlcpy(evt.data.approval_resp.id, s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    stop_approval_timers();
}

static lv_obj_t *screen_boot_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    const ui_palette_t *p = ui_theme_palette();
    ui_theme_style_root(scr);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 272, 126);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -6);
    ui_theme_style_panel(card);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "SMART BUDDY");
    lv_obj_set_style_text_color(title, p->accent, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Booting Runtime\nInitializing HAL + LVGL");
    lv_obj_set_style_text_color(sub, p->text_muted, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 14);

    lv_obj_t *pill = ui_theme_create_pill(scr, "ESP32-S3", p->text, p->panel_alt);
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -22);
    return scr;
}

static lv_obj_t *screen_main_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    /* ── Title bar ────────────────────────────────────────────────── */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "SmartBuddy");

    /* Title text: dynamic service summary */
    s_main_title_label = lv_label_create(bar);
    lv_label_set_text(s_main_title_label, "Ready");
    lv_obj_set_style_text_color(s_main_title_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_main_title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_main_title_label, 236);
    lv_label_set_long_mode(s_main_title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(s_main_title_label, LV_ALIGN_LEFT_MID, 12, 0);

    /* BLE indicator — right side of title bar */
    s_ble_indicator = lv_label_create(bar);
    lv_label_set_text(s_ble_indicator, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_ble_indicator, p->text_muted, 0);
    lv_obj_set_style_text_font(s_ble_indicator, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ble_indicator, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *hero = lv_obj_create(scr);
    lv_obj_set_size(hero, 304, 132);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 46);
    ui_theme_style_panel(hero);

    /* ── Persona ASCII art label — centered, monospace font ─────── */
    s_persona_label = lv_label_create(hero);
    lv_label_set_text(s_persona_label, "");
    lv_obj_set_style_text_color(s_persona_label, p->text, 0);
    lv_obj_set_style_text_font(s_persona_label, &lv_font_unscii_8, 0);
    lv_obj_set_size(s_persona_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_persona_label, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_align(s_persona_label, LV_TEXT_ALIGN_LEFT, 0);

    /* ── Content / msg label — centered below state ──────────────── */
    s_main_content_label = lv_label_create(hero);
    lv_label_set_text(s_main_content_label, "Connect via Hardware Buddy");
    lv_obj_set_style_text_color(s_main_content_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_main_content_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_main_content_label, 272);
    lv_label_set_long_mode(s_main_content_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_main_content_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_align(s_main_content_label, LV_TEXT_ALIGN_CENTER, 0);

    /* ── Passkey overlay (hidden by default, shown during BLE pairing) ─ */
    s_passkey_label = lv_label_create(scr);
    lv_label_set_text(s_passkey_label, "");
    lv_obj_set_style_text_color(s_passkey_label, p->warning, 0);
    lv_obj_set_style_text_font(s_passkey_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(s_passkey_label, p->bg, 0);
    lv_obj_set_style_bg_opa(s_passkey_label, LV_OPA_90, 0);
    lv_obj_set_size(s_passkey_label, 320, 240);
    lv_obj_align(s_passkey_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(s_passkey_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);

    /* ── HUD transcript area (bottom of screen) ────────────────────── */
    s_transcript_area = lv_obj_create(scr);
    lv_obj_set_size(s_transcript_area, 304, 58);
    lv_obj_align(s_transcript_area, LV_ALIGN_BOTTOM_MID, 0, -6);
    ui_theme_style_panel_alt(s_transcript_area);

    s_transcript_label = lv_label_create(s_transcript_area);
    lv_label_set_text(s_transcript_label, "");
    lv_obj_set_style_text_color(s_transcript_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_transcript_label, &lv_font_unscii_8, 0);
    lv_obj_set_width(s_transcript_label, 286);
    lv_label_set_long_mode(s_transcript_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_transcript_label, LV_ALIGN_TOP_LEFT, 2, 2);

    return scr;
}

static lv_obj_t *screen_approval_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 304, 188);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 12);
    ui_theme_style_panel(panel);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Permission Required");
    lv_obj_set_style_text_color(title, p->warning, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_approval_tool = lv_label_create(panel);
    lv_label_set_text(s_approval_tool, "Tool: -");
    lv_obj_set_style_text_color(s_approval_tool, p->text, 0);
    lv_obj_align(s_approval_tool, LV_ALIGN_TOP_LEFT, 10, 36);

    s_approval_hint = lv_label_create(panel);
    lv_label_set_text(s_approval_hint, "");
    lv_obj_set_width(s_approval_hint, 284);
    lv_label_set_long_mode(s_approval_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_approval_hint, p->text_muted, 0);
    lv_obj_align(s_approval_hint, LV_ALIGN_TOP_LEFT, 10, 58);

    /* Approve button */
    lv_obj_t *btn_ok = lv_btn_create(panel);
    lv_obj_set_size(btn_ok, 136, 40);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_ok, p->success, 0);
    lv_obj_set_style_text_color(btn_ok, lv_color_black(), 0);
    lv_obj_add_event_cb(btn_ok, approve_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "APPROVE");
    lv_obj_center(lbl_ok);

    /* Deny button */
    lv_obj_t *btn_no = lv_btn_create(panel);
    lv_obj_set_size(btn_no, 136, 40);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(btn_no, p->danger, 0);
    lv_obj_set_style_text_color(btn_no, lv_color_black(), 0);
    lv_obj_add_event_cb(btn_no, deny_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "DENY");
    lv_obj_center(lbl_no);

    /* Countdown arc */
    s_approval_arc = lv_arc_create(panel);
    lv_obj_set_size(s_approval_arc, 34, 34);
    lv_arc_set_range(s_approval_arc, 0, 100);
    lv_arc_set_value(s_approval_arc, 100);
    lv_obj_align(s_approval_arc, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_arc_color(s_approval_arc, p->warning, LV_PART_INDICATOR);

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
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    /* Title bar with back button */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "System Status");

    ui_theme_create_back_button(bar, status_back_cb);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 304, 188);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 42);
    ui_theme_style_panel(panel);

    /* Stat rows (icon + value) */
    s_status_tokens    = make_stat_row(panel, 10,  LV_SYMBOL_CHARGE,    "-- tokens");
    s_status_sessions  = make_stat_row(panel, 42,  LV_SYMBOL_REFRESH,   "-- sessions");
    s_status_approvals = make_stat_row(panel, 74, LV_SYMBOL_OK,        "-- approved / -- denied");
    s_status_heap      = make_stat_row(panel, 106, LV_SYMBOL_SETTINGS,  "-- KB free");
    s_status_transport = make_stat_row(panel, 138, LV_SYMBOL_BLUETOOTH, "disconnected");

    lv_obj_set_style_text_color(s_status_tokens, p->text, 0);
    lv_obj_set_style_text_color(s_status_sessions, p->text, 0);
    lv_obj_set_style_text_color(s_status_approvals, p->text, 0);
    lv_obj_set_style_text_color(s_status_heap, p->text, 0);
    lv_obj_set_style_text_color(s_status_transport, p->text, 0);

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

static void settings_info_btn_cb(lv_event_t *e)
{
    ui_screen_info_reset();
    ui_manager_push(UI_SCREEN_INFO, UI_ANIM_SLIDE_LEFT);
}

static void settings_back_btn_cb(lv_event_t *e)
{
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

static lv_obj_t *screen_settings_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    /* Title bar */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "Settings");
    ui_theme_create_back_button(bar, settings_back_btn_cb);

    /* Menu list */
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 304, 188);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(list, p->panel, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, p->border, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_style_radius(list, 10, 0);

    lv_obj_t *btn_st = lv_list_add_btn(list, LV_SYMBOL_LIST, "Device Status");
    ui_theme_style_panel_alt(btn_st);
    lv_obj_set_style_text_color(btn_st, p->text, 0);
    lv_obj_add_event_cb(btn_st, settings_status_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_info = lv_list_add_btn(list, LV_SYMBOL_EDIT, "Info");
    ui_theme_style_panel_alt(btn_info);
    lv_obj_set_style_text_color(btn_info, p->text, 0);
    lv_obj_add_event_cb(btn_info, settings_info_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ble = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "BLE Debug");
    ui_theme_style_panel_alt(btn_ble);
    lv_obj_set_style_text_color(btn_ble, p->text, 0);
    lv_obj_add_event_cb(btn_ble, settings_ble_debug_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_AUDIO, "Audio Debug");
    ui_theme_style_panel_alt(btn);
    lv_obj_set_style_text_color(btn, p->text, 0);
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
    case UI_SCREEN_STATS:    return screen_stats_create();
    case UI_SCREEN_INFO:     return screen_info_create();
    case UI_SCREEN_MENU:     return screen_menu_create();
    case UI_SCREEN_CLOCK:    return screen_clock_create();
    default:                 return NULL;
    }
}

static void notify_screen_lifecycle(ui_screen_id_t leaving, ui_screen_id_t entering)
{
    if (leaving == UI_SCREEN_DEBUG)       ui_screen_debug_on_hide();
    if (entering == UI_SCREEN_DEBUG)      ui_screen_debug_on_show();
    if (leaving == UI_SCREEN_BLE_DEBUG)   ui_screen_ble_debug_on_hide();
    if (entering == UI_SCREEN_BLE_DEBUG)  ui_screen_ble_debug_on_show();
    if (entering == UI_SCREEN_CLOCK)      ui_screen_clock_start();
    if (leaving == UI_SCREEN_CLOCK)       ui_screen_clock_stop();
    if (entering == UI_SCREEN_STATS)      ui_screen_stats_refresh();

    /* Wake screen from screen-off on any navigation */
    if (s_screen_dimmed) {
        extern hal_handles_t g_hal;
        if (g_hal.display)
            g_hal.display->backlight_set(g_hal.display, s_saved_brightness);
        s_screen_dimmed = false;
        /* Reset timer to dim phase */
        if (s_screenoff_timer) {
            lv_timer_set_period(s_screenoff_timer, 15000);
            lv_timer_set_repeat_count(s_screenoff_timer, 1);
            lv_timer_pause(s_screenoff_timer);
        }
    }
}

esp_err_t ui_manager_init(void)
{
    /* All LVGL object/timer creation must happen inside the port lock because
     * the LVGL task is already running by the time we get here. */
    if (!lvgl_port_lock(0)) return ESP_FAIL;

    for (int i = 0; i < UI_SCREEN_MAX; i++) {
        s_screens[i] = create_screen((ui_screen_id_t)i);
    }
    /* One-shot screen-off timer; reset on each activity.
     * Stage 1 (15s): dim to 20%. Stage 2 (30s): turn off completely. */
    s_screenoff_timer = lv_timer_create(screenoff_timer_cb, 15000, NULL);
    lv_timer_set_repeat_count(s_screenoff_timer, 1);

    persona_driver_init(persona_frame_cb, NULL);

    lvgl_port_unlock();
    return ESP_OK;
}

void ui_manager_deinit(void)
{
    persona_driver_deinit();
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
    extern hal_handles_t g_hal;

    switch (new_state) {
    case SM_STATE_SLEEP:
        ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_NONE);
        /* No transport connected — screen off immediately unless pairing passkey is active. */
        if (lvgl_port_lock(100)) {
            if (s_screenoff_timer) lv_timer_pause(s_screenoff_timer);
            lvgl_port_unlock();
        }
        if (g_hal.display) {
            uint32_t pk = transport_ble_get_passkey();
            g_hal.display->backlight_set(g_hal.display, pk > 0 ? 100 : 0);
        }
        if (g_hal.led)     g_hal.led->off(g_hal.led);
        break;

    case SM_STATE_ATTENTION:
        ui_manager_push(UI_SCREEN_APPROVAL, UI_ANIM_SLIDE_LEFT);
        /* Keep screen on, pause inactivity timer during approval */
        if (lvgl_port_lock(100)) {
            if (s_screenoff_timer) lv_timer_pause(s_screenoff_timer);
            lvgl_port_unlock();
        }
        if (g_hal.display) g_hal.display->backlight_set(g_hal.display, 100);
        /* Red alert blink */
        if (g_hal.led) {
            g_hal.led->set_rgb(g_hal.led, 255, 0, 0);
            g_hal.led->blink(g_hal.led, 200, 200, -1);
        }
        break;

    default:
        if (old_state == SM_STATE_ATTENTION) {
            ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
        }
        /* Screen on; reset inactivity timer — screen stays on while connected */
        if (g_hal.display) g_hal.display->backlight_set(g_hal.display, 100);
        s_screen_dimmed = false;
        if (lvgl_port_lock(100)) {
            if (s_screenoff_timer) {
                lv_timer_set_period(s_screenoff_timer, 15000);
                lv_timer_set_repeat_count(s_screenoff_timer, 1);
                lv_timer_pause(s_screenoff_timer);
            }
            lvgl_port_unlock();
        }
        /* LED color per state */
        if (g_hal.led) {
            switch (new_state) {
            case SM_STATE_IDLE:
                g_hal.led->set_rgb(g_hal.led, 0, 150, 50);
                g_hal.led->blink(g_hal.led, 1200, 1200, -1);
                break;
            case SM_STATE_BUSY:
                g_hal.led->set_rgb(g_hal.led, 0, 100, 220);
                g_hal.led->blink(g_hal.led, 400, 400, -1);
                break;
            case SM_STATE_CELEBRATE:
                g_hal.led->set_rgb(g_hal.led, 255, 200, 0);
                g_hal.led->blink(g_hal.led, 150, 150, -1);
                break;
            case SM_STATE_DIZZY:
                g_hal.led->set_rgb(g_hal.led, 255, 100, 0);
                g_hal.led->blink(g_hal.led, 100, 100, -1);
                break;
            case SM_STATE_HEART:
                g_hal.led->set_rgb(g_hal.led, 255, 50, 100);
                g_hal.led->blink(g_hal.led, 700, 700, -1);
                break;
            default:
                g_hal.led->off(g_hal.led);
                break;
            }
        }
        break;
    }

    ui_screen_main_set_state(new_state);
}

void ui_screen_main_set_state(sm_state_t state)
{
    if (state >= SM_STATE_MAX) return;
    if (!lvgl_port_lock(100)) {
        return;
    }
    if (s_persona_label) {
        lv_color_t c;
        switch (state) {
        case SM_STATE_IDLE:      c = lv_color_make(0x00, 0xCC, 0x44); break;
        case SM_STATE_BUSY:      c = lv_color_make(0x44, 0xBB, 0xFF); break;
        case SM_STATE_ATTENTION: c = lv_color_make(0xFF, 0x88, 0x00); break;
        case SM_STATE_CELEBRATE: c = lv_color_make(0xFF, 0xFF, 0x44); break;
        case SM_STATE_DIZZY:     c = lv_color_make(0xFF, 0x66, 0x88); break;
        case SM_STATE_HEART:     c = lv_color_make(0xFF, 0x44, 0x66); break;
        default:                 c = lv_color_make(0x55, 0x55, 0x55); break;
        }
        lv_obj_set_style_text_color(s_persona_label, c, 0);
    }
    persona_driver_set_state(state);
    lvgl_port_unlock();
}

void ui_screen_main_set_token_count(uint32_t tokens)
{
    if (!lvgl_port_lock(100)) return;
    if (s_main_title_label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Tokens %lu", (unsigned long)tokens);
        lv_label_set_text(s_main_title_label, buf);
    }
    lvgl_port_unlock();
}

void ui_screen_main_set_ble_connected(bool connected)
{
    const ui_palette_t *p = ui_theme_palette();
    if (lvgl_port_lock(100)) {
        if (s_ble_indicator) {
            lv_color_t color = connected
                ? p->success
                : p->text_muted;
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
    const ui_palette_t *p = ui_theme_palette();
    if (!msg) msg = "";
    if (!lvgl_port_lock(100)) return;
    if (s_main_content_label) {
        lv_label_set_text(s_main_content_label, msg);
        /* Amber when there's Claude content, dim grey when empty/ready */
        bool active = msg[0] && strcmp(msg, "ready") != 0;
        lv_color_t c = active
            ? p->warning
            : p->text_muted;
        lv_obj_set_style_text_color(s_main_content_label, c, 0);
    }
    lvgl_port_unlock();
}

void ui_screen_main_set_entries(const char (*entries)[92], uint8_t n)
{
    if (!lvgl_port_lock(100)) return;

    if (s_transcript_area) {
        if (s_transcript_visible && n > 0) {
            /* Build transcript text from entries */
            char buf[512] = {0};
            size_t pos = 0;
            for (uint8_t i = 0; i < n && i < 8; i++) {
                int written = snprintf(buf + pos, sizeof(buf) - pos,
                                       "%s\n", entries[i]);
                if (written > 0 && pos + written < sizeof(buf))
                    pos += written;
            }
            if (s_transcript_label)
                lv_label_set_text(s_transcript_label, buf);
            lv_obj_clear_flag(s_transcript_area, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_transcript_area, LV_OBJ_FLAG_HIDDEN);
        }
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
