#include <string.h>
#include "lvgl.h"
#include "ui/ui_fonts.h"

lv_font_t g_font_14_cjk;

void ui_fonts_init(void)
{
    memcpy(&g_font_14_cjk, &lv_font_montserrat_14, sizeof(lv_font_t));
    g_font_14_cjk.fallback = &lv_font_source_han_sans_sc_14_cjk;
}
