#include <string.h>
#include <stdio.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "ui/ui_fonts.h"
#include "agent_core.h"
#include "buddy_hal/agent_events.h"

#ifndef CONFIG_UI_APPROVAL_TIMEOUT_S
#define CONFIG_UI_APPROVAL_TIMEOUT_S 30
#endif

static lv_obj_t  *s_approval_tool    = NULL;
static lv_obj_t  *s_approval_hint    = NULL;
static char        s_approval_id_store[64];
static lv_obj_t  *s_approval_arc     = NULL;
static lv_timer_t *s_arc_timer       = NULL;
static uint32_t    s_arc_timeout_ms  = 0;
static uint32_t    s_arc_elapsed_ms  = 0;
static lv_timer_t *s_approval_timer  = NULL;

static void stop_approval_timers(void)
{
    if (s_arc_timer)      { lv_timer_del(s_arc_timer);      s_arc_timer      = NULL; }
    if (s_approval_timer) { lv_timer_del(s_approval_timer); s_approval_timer = NULL; }
}

static void arc_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_arc_elapsed_ms += 250;
    if (s_arc_timeout_ms > 0 && s_approval_arc) {
        int32_t pct = 100 - (int32_t)(s_arc_elapsed_ms * 100 / s_arc_timeout_ms);
        if (pct < 0) pct = 0;
        lv_arc_set_value(s_approval_arc, (int16_t)pct);
    }
}

static void approval_timeout_cb(lv_timer_t *t)
{
    (void)t;
    if (s_arc_timer) { lv_timer_del(s_arc_timer); s_arc_timer = NULL; }
    if (s_approval_arc) lv_arc_set_value(s_approval_arc, 0);

    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = false,
        .timestamp_us = 0,
    };
    strlcpy(evt.data.approval_resp.id, s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    s_approval_timer = NULL;
}

static void approve_btn_cb(lv_event_t *e)
{
    (void)e;
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
    (void)e;
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = false,
    };
    strlcpy(evt.data.approval_resp.id, s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    stop_approval_timers();
}

lv_obj_t *screen_approval_create(void)
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
    lv_obj_set_style_text_font(s_approval_hint, &g_font_14_cjk, 0);
    lv_obj_align(s_approval_hint, LV_ALIGN_TOP_LEFT, 10, 58);

    lv_obj_t *btn_ok = lv_btn_create(panel);
    lv_obj_set_size(btn_ok, 136, 40);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_ok, p->success, 0);
    lv_obj_set_style_text_color(btn_ok, lv_color_black(), 0);
    lv_obj_add_event_cb(btn_ok, approve_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "APPROVE");
    lv_obj_center(lbl_ok);

    lv_obj_t *btn_no = lv_btn_create(panel);
    lv_obj_set_size(btn_no, 136, 40);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(btn_no, p->danger, 0);
    lv_obj_set_style_text_color(btn_no, lv_color_black(), 0);
    lv_obj_add_event_cb(btn_no, deny_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "DENY");
    lv_obj_center(lbl_no);

    s_approval_arc = lv_arc_create(panel);
    lv_obj_set_size(s_approval_arc, 34, 34);
    lv_arc_set_range(s_approval_arc, 0, 100);
    lv_arc_set_value(s_approval_arc, 100);
    lv_obj_align(s_approval_arc, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_arc_color(s_approval_arc, p->warning, LV_PART_INDICATOR);

    return scr;
}

void ui_screen_approval_stop_timers(void)
{
    stop_approval_timers();
}

void ui_screen_approval_set_prompt(const char *tool, const char *hint, const char *id)
{
    if (!lvgl_port_lock(100)) return;

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

    stop_approval_timers();
    s_arc_timeout_ms = CONFIG_UI_APPROVAL_TIMEOUT_S * 1000;
    s_arc_elapsed_ms = 0;
    s_arc_timer = lv_timer_create(arc_tick_cb, 250, NULL);
    s_approval_timer = lv_timer_create(approval_timeout_cb, s_arc_timeout_ms, NULL);
    lv_timer_set_repeat_count(s_approval_timer, 1);

    lvgl_port_unlock();
}

void ui_approval_handle_key(bool approved)
{
    agent_event_t evt = {
        .type = AGENT_EVT_APPROVAL_RESOLVED,
        .data.approval_resp.approved = approved,
    };
    strlcpy(evt.data.approval_resp.id, s_approval_id_store,
            sizeof(evt.data.approval_resp.id));
    agent_core_post_event(&evt);
    if (lvgl_port_lock(100)) {
        stop_approval_timers();
        lvgl_port_unlock();
    }
}
