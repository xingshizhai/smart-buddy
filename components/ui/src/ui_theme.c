#include "ui/ui_theme.h"

#include <stdbool.h>

static ui_palette_t s_palette;
static bool s_palette_inited = false;

static void init_palette(void)
{
    if (s_palette_inited) return;
    s_palette.bg = lv_color_make(0x00, 0x00, 0x00);
    s_palette.bg_top = lv_color_make(0x12, 0x12, 0x18);
    s_palette.panel = lv_color_make(0x1A, 0x1D, 0x24);
    s_palette.panel_alt = lv_color_make(0x24, 0x29, 0x34);
    s_palette.border = lv_color_make(0x35, 0x3A, 0x48);
    s_palette.text = lv_color_make(0xF4, 0xF5, 0xF7);
    s_palette.text_muted = lv_color_make(0xA8, 0xAF, 0xBC);
    s_palette.accent = lv_color_make(0x4F, 0xD1, 0xFF);
    s_palette.success = lv_color_make(0x47, 0xD4, 0x86);
    s_palette.warning = lv_color_make(0xFF, 0xC1, 0x45);
    s_palette.danger = lv_color_make(0xFF, 0x73, 0x73);
    s_palette_inited = true;
}

const ui_palette_t *ui_theme_palette(void)
{
    init_palette();
    return &s_palette;
}

static void style_panel_common(lv_obj_t *obj, lv_color_t bg)
{
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, s_palette.border, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(obj, 10, 0);
    lv_obj_set_style_shadow_spread(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_theme_style_root(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, s_palette.bg, 0);
    lv_obj_set_style_bg_grad_color(obj, s_palette.bg_top, 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_theme_style_panel(lv_obj_t *obj)
{
    style_panel_common(obj, s_palette.panel);
}

void ui_theme_style_panel_alt(lv_obj_t *obj)
{
    style_panel_common(obj, s_palette.panel_alt);
}

void ui_theme_style_button_primary(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, s_palette.accent, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_text_color(obj, lv_color_black(), 0);
}

void ui_theme_style_button_ghost(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, s_palette.panel_alt, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, s_palette.border, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_text_color(obj, s_palette.text, 0);
}

lv_obj_t *ui_theme_create_title_bar(lv_obj_t *parent, const char *title)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 320, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, s_palette.panel_alt, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title ? title : "");
    lv_obj_set_style_text_color(lbl, s_palette.text, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return bar;
}

lv_obj_t *ui_theme_create_back_button(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 64, 24);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 0, 0);
    ui_theme_style_button_ghost(btn);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

lv_obj_t *ui_theme_create_pill(lv_obj_t *parent, const char *text, lv_color_t fg, lv_color_t bg)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, bg, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_radius(pill, 10, 0);
    lv_obj_set_style_pad_ver(pill, 4, 0);
    lv_obj_set_style_pad_hor(pill, 8, 0);

    lv_obj_t *lbl = lv_label_create(pill);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_color(lbl, fg, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return pill;
}
