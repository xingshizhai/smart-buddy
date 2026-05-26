#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"

#define TAG "UI_CLOCK"

static lv_obj_t    *s_scr         = NULL;
static lv_obj_t    *s_time_label  = NULL;
static lv_obj_t    *s_date_label  = NULL;
static lv_timer_t  *s_update_timer = NULL;

static void back_btn_cb(lv_event_t *e)
{
    ui_screen_clock_stop();
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

static void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);

    if (!lvgl_port_lock(100)) return;

    char buf[64];
    if (s_time_label) {
        strftime(buf, sizeof(buf), "%H:%M", tm_info);
        lv_label_set_text(s_time_label, buf);
    }
    if (s_date_label) {
        strftime(buf, sizeof(buf), "%A, %B %d, %Y", tm_info);
        lv_label_set_text(s_date_label, buf);
    }

    lvgl_port_unlock();
}

lv_obj_t *screen_clock_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);
    s_scr = scr;

    lv_obj_t *bar = ui_theme_create_title_bar(scr, "Clock");

    lv_obj_t *btn_bk = lv_btn_create(bar);
    lv_obj_set_size(btn_bk, 58, 24);
    lv_obj_align(btn_bk, LV_ALIGN_LEFT_MID, 0, 0);
    ui_theme_style_button_ghost(btn_bk);
    lv_obj_add_event_cb(btn_bk, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bk = lv_label_create(btn_bk);
    lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_bk, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_bk);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 304, 188);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 42);
    ui_theme_style_panel(panel);

    /* Time label — large, centered */
    s_time_label = lv_label_create(panel);
    lv_label_set_text(s_time_label, "00:00");
    lv_obj_set_style_text_color(s_time_label, p->accent, 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 26);

    /* Date label — smaller, below time */
    s_date_label = lv_label_create(panel);
    lv_label_set_text(s_date_label, "");
    lv_obj_set_style_text_color(s_date_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_MID, 0, 92);

    /* Update immediately */
    clock_update_cb(NULL);

    return scr;
}

void ui_screen_clock_start(void)
{
    if (s_update_timer) return;
    s_update_timer = lv_timer_create(clock_update_cb, 60000, NULL);
    /* First update immediately */
    clock_update_cb(NULL);
}

void ui_screen_clock_stop(void)
{
    if (s_update_timer) {
        lv_timer_del(s_update_timer);
        s_update_timer = NULL;
    }
}
