#include <stdio.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "agent_stats.h"
#include "transport/transport.h"

static lv_obj_t *s_status_tokens     = NULL;
static lv_obj_t *s_status_sessions   = NULL;
static lv_obj_t *s_status_approvals  = NULL;
static lv_obj_t *s_status_heap       = NULL;
static lv_obj_t *s_status_transport  = NULL;

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

static void status_back_cb(lv_event_t *e) { (void)e; ui_manager_pop(UI_ANIM_SLIDE_RIGHT); }

lv_obj_t *screen_status_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    lv_obj_t *bar = ui_theme_create_title_bar(scr, "System Status");
    ui_theme_create_back_button(bar, status_back_cb);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 304, 188);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 42);
    ui_theme_style_panel(panel);

    s_status_tokens    = make_stat_row(panel, 10,  LV_SYMBOL_CHARGE,    "-- tokens");
    s_status_sessions  = make_stat_row(panel, 42,  LV_SYMBOL_REFRESH,   "-- sessions");
    s_status_approvals = make_stat_row(panel, 74,  LV_SYMBOL_OK,        "-- approved / -- denied");
    s_status_heap      = make_stat_row(panel, 106, LV_SYMBOL_SETTINGS,  "-- KB free");
    s_status_transport = make_stat_row(panel, 138, LV_SYMBOL_BLUETOOTH, "disconnected");

    lv_obj_set_style_text_color(s_status_tokens,    p->text, 0);
    lv_obj_set_style_text_color(s_status_sessions,  p->text, 0);
    lv_obj_set_style_text_color(s_status_approvals, p->text, 0);
    lv_obj_set_style_text_color(s_status_heap,      p->text, 0);
    lv_obj_set_style_text_color(s_status_transport, p->text, 0);

    return scr;
}

void ui_screen_status_refresh(void)
{
    agent_stats_t st = agent_stats_get();
    uint32_t free_kb = esp_get_free_heap_size() / 1024;

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
    if (s_status_transport)
        lv_label_set_text(s_status_transport, transport_str);

    lvgl_port_unlock();
}
