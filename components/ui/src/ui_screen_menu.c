#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"

#define TAG "UI_MENU"

static lv_obj_t *s_scr   = NULL;
static lv_obj_t *s_list  = NULL;

static void menu_item_cb(lv_event_t *e)
{
    const char *label = lv_list_get_btn_text(s_list, lv_event_get_target(e));
    if (!label) return;

    ESP_LOGI(TAG, "menu selected: %s", label);

    if (strcmp(label, "Pet Stats") == 0) {
        ui_manager_pop(UI_ANIM_NONE); /* close menu first */
        ui_manager_push(UI_SCREEN_STATS, UI_ANIM_SLIDE_LEFT);
    } else if (strcmp(label, "Info") == 0) {
        ui_manager_pop(UI_ANIM_NONE);
        ui_manager_push(UI_SCREEN_INFO, UI_ANIM_SLIDE_LEFT);
    } else if (strcmp(label, "Settings") == 0) {
        ui_manager_pop(UI_ANIM_NONE);
        ui_manager_push(UI_SCREEN_SETTINGS, UI_ANIM_SLIDE_LEFT);
    } else if (strcmp(label, "Clock") == 0) {
        ui_manager_pop(UI_ANIM_NONE);
        ui_manager_push(UI_SCREEN_CLOCK, UI_ANIM_FADE);
    }
}

static void back_cb(lv_event_t *e)
{
    ui_manager_pop(UI_ANIM_FADE);
}

lv_obj_t *screen_menu_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_80, 0);
    s_scr = scr;

    /* Title bar */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "Menu");
    ui_theme_create_back_button(bar, back_cb);

    /* Menu list */
    s_list = lv_list_create(scr);
    lv_obj_set_size(s_list, 304, 188);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(s_list, p->panel, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_list, p->border, 0);
    lv_obj_set_style_border_width(s_list, 1, 0);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_style_pad_all(s_list, 8, 0);
    lv_obj_set_style_radius(s_list, 10, 0);

    const char *items[] = {"Pet Stats", "Info", "Settings", "Clock"};
    const char *icons[] = {LV_SYMBOL_CHARGE, LV_SYMBOL_EDIT, LV_SYMBOL_SETTINGS, LV_SYMBOL_EYE_OPEN};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_list_add_btn(s_list, icons[i], items[i]);
        ui_theme_style_panel_alt(btn);
        lv_obj_set_style_text_color(btn, p->text, 0);
        lv_obj_add_event_cb(btn, menu_item_cb, LV_EVENT_CLICKED, NULL);
    }

    return scr;
}

void ui_screen_menu_set_selection(const char *label)
{
    (void)label; /* Not needed — lv_list handles selection */
}
