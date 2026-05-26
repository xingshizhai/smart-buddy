#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "transport/transport.h"

#define TAG "UI_INFO"

#define INFO_PAGE_COUNT 6

static lv_obj_t *s_scr          = NULL;
static lv_obj_t *s_page_title   = NULL;
static lv_obj_t *s_page_body    = NULL;
static lv_obj_t *s_page_dots[INFO_PAGE_COUNT];
static uint8_t   s_current_page = 0;

static const char *s_titles[INFO_PAGE_COUNT] = {
    "About", "Buttons", "Claude", "Device", "Bluetooth", "Credits"
};

static void back_btn_cb(lv_event_t *e)
{
    ui_screen_info_reset();
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

static void next_btn_cb(lv_event_t *e)
{
    ui_screen_info_next_page();
}

static void show_page(uint8_t idx)
{
    const ui_palette_t *p = ui_theme_palette();
    if (idx >= INFO_PAGE_COUNT) return;
    s_current_page = idx;

    if (!lvgl_port_lock(100)) return;

    lv_label_set_text(s_page_title, s_titles[idx]);

    char buf[512];
    switch (idx) {
    case 0: /* About */
        snprintf(buf, sizeof(buf),
                 "Claude Desktop Buddy v1.0\n\n"
                 "ESP32-S3-BOX-3\n"
                 "320x240 LVGL Display\n"
                 "NimBLE BLE Stack\n\n"
                 "A companion device for\n"
                 "Anthropic's Claude Desktop");
        break;
    case 1: /* Buttons */
        snprintf(buf, sizeof(buf),
                 "BOOT (Center Button)\n"
                 "  Short: Push-to-Talk\n"
                 "  Long:  Open Menu\n\n"
                 "LEFT (Mute Button)\n"
                 "  Short: Back / Mute\n"
                 "  Long:  Pet Stats\n\n"
                 "During Approval:\n"
                 "  BOOT Long: Approve\n"
                 "  LEFT Short: Deny");
        break;
    case 2: /* Claude */
        snprintf(buf, sizeof(buf),
                 "Token Milestones: Every 50K\n"
                 "Heartbeat: Every 10s\n\n"
                 "Sessions tracked locally.\n"
                 "Stats persist across reboots.\n\n"
                 "Protocol: claude-desktop-buddy\n"
                 "JSON-lines over BLE NUS");
        break;
    case 3: /* Device */
        snprintf(buf, sizeof(buf),
                 "Chip: ESP32-S3\n"
                 "Free Heap: %lu KB\n"
                 "Board: ESP32-S3-BOX-3\n\n"
                 "Display: 320x240 ILI9342\n"
                 "Touch: FT5x06\n"
                 "IMU: QMI8658",
                 (unsigned long)(esp_get_free_heap_size() / 1024));
        break;
    case 4: /* Bluetooth */
        snprintf(buf, sizeof(buf),
                 "Service: NUS (Nordic UART)\n"
                 "Device: %s\n"
                 "MTU: %d\n"
                 "Bonded: Yes (LE SC)\n\n"
                 "UUID: 6e400001-b5a3-f393-\n"
                 "       e0a9-e50e24dcca9e",
                 transport_ble_get_device_name(),
                 (int)transport_ble_get_mtu());
        break;
    case 5: /* Credits */
        snprintf(buf, sizeof(buf),
                 "Official Project:\n"
                 "github.com/anthropics/\n"
                 "  claude-desktop-buddy\n\n"
                 "ESP-IDF v6.0.1\n"
                 "LVGL 9.x\n"
                 "NimBLE Stack");
        break;
    }
    lv_label_set_text(s_page_body, buf);

    /* Update dots */
    for (int i = 0; i < INFO_PAGE_COUNT; i++) {
        if (s_page_dots[i]) {
            lv_color_t c = (i == idx)
                ? p->accent
                : p->text_muted;
            lv_obj_set_style_text_color(s_page_dots[i], c, 0);
        }
    }

    lvgl_port_unlock();
}

lv_obj_t *screen_info_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);
    s_scr = scr;

    /* Title bar */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "Info");
    ui_theme_create_back_button(bar, back_btn_cb);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 304, 188);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 42);
    ui_theme_style_panel(panel);

    /* Page title */
    s_page_title = lv_label_create(panel);
    lv_label_set_text(s_page_title, "About");
    lv_obj_set_style_text_color(s_page_title, p->accent, 0);
    lv_obj_set_style_text_font(s_page_title, &lv_font_montserrat_24, 0);
    lv_obj_align(s_page_title, LV_ALIGN_TOP_MID, 0, 4);

    /* Page body */
    s_page_body = lv_label_create(panel);
    lv_label_set_text(s_page_body, "");
    lv_obj_set_style_text_color(s_page_body, p->text, 0);
    lv_obj_set_style_text_font(s_page_body, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_page_body, 282);
    lv_label_set_long_mode(s_page_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_page_body, LV_ALIGN_TOP_LEFT, 10, 42);
    lv_obj_set_style_text_align(s_page_body, LV_TEXT_ALIGN_LEFT, 0);

    /* Next page button */
    lv_obj_t *btn_next = lv_btn_create(panel);
    lv_obj_set_size(btn_next, 108, 32);
    lv_obj_align(btn_next, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    ui_theme_style_button_primary(btn_next);
    lv_obj_add_event_cb(btn_next, next_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, "Next " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_next);

    /* Page indicator dots */
    int dot_start_x = 10;
    for (int i = 0; i < INFO_PAGE_COUNT; i++) {
        s_page_dots[i] = lv_label_create(panel);
        lv_label_set_text(s_page_dots[i], "\xE2\x80\xA2");  /* bullet dot */
        lv_obj_set_style_text_font(s_page_dots[i], &lv_font_montserrat_16, 0);
        lv_obj_align(s_page_dots[i], LV_ALIGN_BOTTOM_LEFT, dot_start_x + i * 14, -10);
    }

    show_page(0);
    return scr;
}

void ui_screen_info_next_page(void)
{
    uint8_t next = (s_current_page + 1) % INFO_PAGE_COUNT;
    show_page(next);
}

void ui_screen_info_reset(void)
{
    s_current_page = 0;
}
