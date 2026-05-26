#pragma once

#include <stdbool.h>
#include "lvgl.h"

typedef struct {
    lv_color_t bg;
    lv_color_t bg_top;
    lv_color_t panel;
    lv_color_t panel_alt;
    lv_color_t border;
    lv_color_t text;
    lv_color_t text_muted;
    lv_color_t accent;
    lv_color_t success;
    lv_color_t warning;
    lv_color_t danger;
} ui_palette_t;

const ui_palette_t *ui_theme_palette(void);

void ui_theme_style_root(lv_obj_t *obj);
void ui_theme_style_panel(lv_obj_t *obj);
void ui_theme_style_panel_alt(lv_obj_t *obj);
void ui_theme_style_button_primary(lv_obj_t *obj);
void ui_theme_style_button_ghost(lv_obj_t *obj);

lv_obj_t *ui_theme_create_title_bar(lv_obj_t *parent, const char *title);
lv_obj_t *ui_theme_create_back_button(lv_obj_t *parent, lv_event_cb_t cb);
lv_obj_t *ui_theme_create_pill(lv_obj_t *parent, const char *text, lv_color_t fg, lv_color_t bg);
