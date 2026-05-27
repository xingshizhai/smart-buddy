#include <string.h>
#include "lvgl.h"
#include "ui/ui_fonts.h"

extern const lv_font_t lv_font_noto_sans_sc_14;

lv_font_t g_font_14_cjk;

void ui_fonts_init(void)
{
    memcpy(&g_font_14_cjk, &lv_font_montserrat_14, sizeof(lv_font_t));
    g_font_14_cjk.fallback = &lv_font_noto_sans_sc_14;
}
