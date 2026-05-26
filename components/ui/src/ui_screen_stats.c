#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "agent_stats.h"

#define TAG "UI_STATS"

static lv_obj_t *s_scr        = NULL;
static lv_obj_t *s_mood_label = NULL;  /* row of hearts */
static lv_obj_t *s_fed_bar    = NULL;
static lv_obj_t *s_energy_bar = NULL;
static lv_obj_t *s_level_label = NULL;
static lv_obj_t *s_tokens_total = NULL;
static lv_obj_t *s_tokens_today = NULL;

static uint8_t s_energy_pct = 100;
static uint8_t s_fed_pct    = 50;

static void back_btn_cb(lv_event_t *e)
{
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

lv_obj_t *screen_stats_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);
    s_scr = scr;

    /* Title bar */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "Pet Stats");
    ui_theme_create_back_button(bar, back_btn_cb);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 304, 188);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 42);
    ui_theme_style_panel(panel);

    /* ── Mood hearts (row of 5 hearts, filled based on approval rate) ── */
    s_mood_label = lv_label_create(panel);
    lv_label_set_text(s_mood_label, "");
    lv_obj_set_style_text_font(s_mood_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_mood_label, LV_ALIGN_TOP_LEFT, 10, 6);

    /* ── Level (large number centered) ── */
    s_level_label = lv_label_create(panel);
    lv_label_set_text(s_level_label, "Lv 1");
    lv_obj_set_style_text_color(s_level_label, p->accent, 0);
    lv_obj_set_style_text_font(s_level_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_level_label, LV_ALIGN_TOP_MID, 0, 2);

    /* ── Fed progress bar ── */
    lv_obj_t *fed_title = lv_label_create(panel);
    lv_label_set_text(fed_title, "Fed");
    lv_obj_set_style_text_color(fed_title, p->success, 0);
    lv_obj_set_style_text_font(fed_title, &lv_font_montserrat_14, 0);
    lv_obj_align(fed_title, LV_ALIGN_TOP_LEFT, 10, 52);

    s_fed_bar = lv_bar_create(panel);
    lv_obj_set_size(s_fed_bar, 284, 14);
    lv_obj_align(s_fed_bar, LV_ALIGN_TOP_LEFT, 10, 72);
    lv_obj_set_style_bg_color(s_fed_bar, p->panel_alt, 0);
    lv_obj_set_style_bg_color(s_fed_bar, p->success, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_fed_bar, 8, 0);
    lv_obj_set_style_radius(s_fed_bar, 8, LV_PART_INDICATOR);
    lv_bar_set_range(s_fed_bar, 0, 100);
    lv_bar_set_value(s_fed_bar, s_fed_pct, LV_ANIM_OFF);

    /* ── Energy bar ── */
    lv_obj_t *energy_title = lv_label_create(panel);
    lv_label_set_text(energy_title, "Energy");
    lv_obj_set_style_text_color(energy_title, p->warning, 0);
    lv_obj_set_style_text_font(energy_title, &lv_font_montserrat_14, 0);
    lv_obj_align(energy_title, LV_ALIGN_TOP_LEFT, 10, 94);

    s_energy_bar = lv_bar_create(panel);
    lv_obj_set_size(s_energy_bar, 284, 14);
    lv_obj_align(s_energy_bar, LV_ALIGN_TOP_LEFT, 10, 114);
    lv_obj_set_style_bg_color(s_energy_bar, p->panel_alt, 0);
    lv_obj_set_style_bg_color(s_energy_bar, p->warning, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_energy_bar, 8, 0);
    lv_obj_set_style_radius(s_energy_bar, 8, LV_PART_INDICATOR);
    lv_bar_set_range(s_energy_bar, 0, 100);
    lv_bar_set_value(s_energy_bar, s_energy_pct, LV_ANIM_OFF);

    /* ── Token counts ── */
    s_tokens_total = lv_label_create(panel);
    lv_label_set_text(s_tokens_total, "Total: 0");
    lv_obj_set_style_text_color(s_tokens_total, p->text, 0);
    lv_obj_set_style_text_font(s_tokens_total, &lv_font_montserrat_14, 0);
    lv_obj_align(s_tokens_total, LV_ALIGN_TOP_LEFT, 10, 140);

    s_tokens_today = lv_label_create(panel);
    lv_label_set_text(s_tokens_today, "Today: 0");
    lv_obj_set_style_text_color(s_tokens_today, p->text_muted, 0);
    lv_obj_set_style_text_font(s_tokens_today, &lv_font_montserrat_14, 0);
    lv_obj_align(s_tokens_today, LV_ALIGN_TOP_LEFT, 10, 162);

    ui_screen_stats_refresh();
    return scr;
}

void ui_screen_stats_refresh(void)
{
    agent_stats_t st = agent_stats_get();

    if (!lvgl_port_lock(100)) return;

    /* Mood: 5 hearts based on approval rate */
    uint32_t total_appr = st.approvals_granted + st.approvals_denied;
    int filled_hearts = 0;
    if (total_appr > 0) {
        float rate = (float)st.approvals_granted / (float)total_appr;
        filled_hearts = (int)(rate * 5.0f + 0.5f);
    } else {
        filled_hearts = 3; /* neutral */
    }
    if (filled_hearts < 0) filled_hearts = 0;
    if (filled_hearts > 5) filled_hearts = 5;

    char mood_buf[96];
    int pos = 0;
    for (int i = 0; i < 5; i++) {
        pos += snprintf(mood_buf + pos, sizeof(mood_buf) - pos,
                        i < filled_hearts ? "#FF4466♥#" : "#6A7284♥#");
    }
    if (s_mood_label) {
        lv_label_set_recolor(s_mood_label, true);
        lv_label_set_text(s_mood_label, mood_buf);
    }

    /* Level: derived from sessions count (1 level per 5 sessions) */
    uint32_t level = 1 + st.sessions_count / 5;
    if (s_level_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Lv %lu", (unsigned long)level);
        lv_label_set_text(s_level_label, buf);
    }

    /* Token counts */
    if (s_tokens_total) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Total: %lu", (unsigned long)st.tokens_total);
        lv_label_set_text(s_tokens_total, buf);
    }
    if (s_tokens_today) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Today: %lu", (unsigned long)st.tokens_today);
        lv_label_set_text(s_tokens_today, buf);
    }

    /* Bars */
    if (s_fed_bar) lv_bar_set_value(s_fed_bar, s_fed_pct, LV_ANIM_OFF);
    if (s_energy_bar) lv_bar_set_value(s_energy_bar, s_energy_pct, LV_ANIM_OFF);

    lvgl_port_unlock();
}

void ui_screen_stats_set_energy(uint8_t pct)
{
    s_energy_pct = pct;
    if (lvgl_port_lock(100)) {
        if (s_energy_bar) lv_bar_set_value(s_energy_bar, pct, LV_ANIM_OFF);
        lvgl_port_unlock();
    }
}

void ui_screen_stats_set_fed(uint8_t pct)
{
    s_fed_pct = pct;
    if (lvgl_port_lock(100)) {
        if (s_fed_bar) lv_bar_set_value(s_fed_bar, pct, LV_ANIM_OFF);
        lvgl_port_unlock();
    }
}
