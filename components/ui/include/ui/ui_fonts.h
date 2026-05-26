#pragma once
#include "lvgl.h"

/* Montserrat 14 with Source Han Sans SC 14 CJK as fallback.
 * Call ui_fonts_init() once before ui_manager_init(). */
extern lv_font_t g_font_14_cjk;

void ui_fonts_init(void);
