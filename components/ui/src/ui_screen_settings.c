#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "ui/ui_fonts.h"
#include "audio_manager.h"
#include "transport/transport.h"

static lv_obj_t *s_set_ble_val    = NULL;
static lv_obj_t *s_set_wifi_val   = NULL;
static lv_obj_t *s_set_usb_val    = NULL;
static lv_obj_t *s_set_vol_slider = NULL;
static lv_obj_t *s_set_vol_label  = NULL;

static void settings_volume_label_update(int32_t vol)
{
    if (!s_set_vol_label) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%ld%%", (long)vol);
    lv_label_set_text(s_set_vol_label, buf);
}

static void settings_volume_slider_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) return;

    lv_obj_t *slider = lv_event_get_target(e);
    int32_t vol = lv_slider_get_value(slider);
    settings_volume_label_update(vol);
    audio_manager_set_volume((uint8_t)vol);

    if (code == LV_EVENT_RELEASED && vol > 0)
        audio_manager_play_alert_preview();
}

static void settings_refresh_volume(void)
{
    if (!s_set_vol_slider) return;
    uint8_t vol = audio_manager_get_volume();
    lv_slider_set_value(s_set_vol_slider, vol, LV_ANIM_OFF);
    settings_volume_label_update(vol);
}

static void settings_refresh_status(void)
{
    const ui_palette_t *p = ui_theme_palette();
    if (!lvgl_port_lock(100)) return;

    if (s_set_ble_val) {
        transport_state_t st = transport_get_state(TRANSPORT_ID_BLE);
        bool conn = (st == TRANSPORT_STATE_CONNECTED);
        const char *nm = transport_ble_get_device_name();
        char buf[48];
        snprintf(buf, sizeof(buf), conn ? "%s  Connected" : "%s  --",
                 nm ? nm : "?");
        lv_label_set_text(s_set_ble_val, buf);
        lv_obj_set_style_text_color(s_set_ble_val,
                                    conn ? p->success : p->text_muted, 0);
    }

    if (s_set_wifi_val) {
        char buf[24] = "--";
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip = {};
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
                snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
            } else {
                strlcpy(buf, "No IP", sizeof(buf));
            }
        }
        bool conn = (buf[0] != '-');
        lv_label_set_text(s_set_wifi_val, buf);
        lv_obj_set_style_text_color(s_set_wifi_val,
                                    conn ? p->success : p->text_muted, 0);
    }

    if (s_set_usb_val) {
        transport_state_t st = transport_get_state(TRANSPORT_ID_USB);
        bool conn = (st == TRANSPORT_STATE_CONNECTED);
        lv_label_set_text(s_set_usb_val, conn ? "Active" : "--");
        lv_obj_set_style_text_color(s_set_usb_val,
                                    conn ? p->success : p->text_muted, 0);
    }

    lvgl_port_unlock();
}

static void settings_ble_debug_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_push(UI_SCREEN_BLE_DEBUG, UI_ANIM_SLIDE_LEFT);
}

static void settings_debug_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_push(UI_SCREEN_DEBUG, UI_ANIM_SLIDE_LEFT);
}

static void settings_status_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_screen_status_refresh();
    ui_manager_push(UI_SCREEN_STATUS, UI_ANIM_SLIDE_LEFT);
}

static void settings_info_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_screen_info_reset();
    ui_manager_push(UI_SCREEN_INFO, UI_ANIM_SLIDE_LEFT);
}

static void settings_back_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_pop(UI_ANIM_NONE);
}

lv_obj_t *screen_settings_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    lv_obj_t *bar = ui_theme_create_title_bar(scr, "Settings");
    ui_theme_create_back_button(bar, settings_back_btn_cb);

    /* Layout (320×240, title bar h=36):
     *   Status panel : y=40, h=66   (3 rows × 18px in 54px inner, pad=6)
     *   Volume panel : y=110, h=36  (label + slider in 24px inner, pad=6)
     *   Nav list     : y=150, h=90  (shows ~3 buttons; 4th reachable by scroll)
     */
    lv_obj_t *sp = lv_obj_create(scr);
    lv_obj_set_size(sp, 304, 66);
    lv_obj_align(sp, LV_ALIGN_TOP_MID, 0, 40);
    ui_theme_style_panel(sp);
    lv_obj_set_style_pad_all(sp, 6, 0);
    lv_obj_set_style_shadow_width(sp, 0, 0);   /* no shadow — too slow for soft renderer */
    lv_obj_clear_flag(sp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ble_lbl = lv_label_create(sp);
    lv_label_set_text(ble_lbl, LV_SYMBOL_BLUETOOTH " BLE");
    lv_obj_set_style_text_color(ble_lbl, p->text_muted, 0);
    lv_obj_set_style_text_font(ble_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(ble_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_set_ble_val = lv_label_create(sp);
    lv_label_set_text(s_set_ble_val, "--");
    lv_obj_set_style_text_color(s_set_ble_val, p->text_muted, 0);
    lv_obj_set_style_text_font(s_set_ble_val, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_set_ble_val, 200);
    lv_label_set_long_mode(s_set_ble_val, LV_LABEL_LONG_DOT);
    lv_obj_align(s_set_ble_val, LV_ALIGN_TOP_LEFT, 52, 0);

    lv_obj_t *wifi_lbl = lv_label_create(sp);
    lv_label_set_text(wifi_lbl, LV_SYMBOL_WIFI " WiFi");
    lv_obj_set_style_text_color(wifi_lbl, p->text_muted, 0);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_lbl, LV_ALIGN_TOP_LEFT, 0, 20);

    s_set_wifi_val = lv_label_create(sp);
    lv_label_set_text(s_set_wifi_val, "--");
    lv_obj_set_style_text_color(s_set_wifi_val, p->text_muted, 0);
    lv_obj_set_style_text_font(s_set_wifi_val, &lv_font_montserrat_14, 0);
    lv_obj_align(s_set_wifi_val, LV_ALIGN_TOP_LEFT, 52, 20);

    lv_obj_t *usb_lbl = lv_label_create(sp);
    lv_label_set_text(usb_lbl, LV_SYMBOL_USB " USB");
    lv_obj_set_style_text_color(usb_lbl, p->text_muted, 0);
    lv_obj_set_style_text_font(usb_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(usb_lbl, LV_ALIGN_TOP_LEFT, 0, 40);

    s_set_usb_val = lv_label_create(sp);
    lv_label_set_text(s_set_usb_val, "--");
    lv_obj_set_style_text_color(s_set_usb_val, p->text_muted, 0);
    lv_obj_set_style_text_font(s_set_usb_val, &lv_font_montserrat_14, 0);
    lv_obj_align(s_set_usb_val, LV_ALIGN_TOP_LEFT, 52, 40);

    lv_obj_t *vp = lv_obj_create(scr);
    lv_obj_set_size(vp, 304, 36);
    lv_obj_align(vp, LV_ALIGN_TOP_MID, 0, 110);
    ui_theme_style_panel(vp);
    lv_obj_set_style_pad_all(vp, 6, 0);
    lv_obj_set_style_shadow_width(vp, 0, 0);   /* no shadow */
    lv_obj_clear_flag(vp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *vol_lbl = lv_label_create(vp);
    lv_label_set_text(vol_lbl, LV_SYMBOL_VOLUME_MAX " Alert Volume");
    lv_obj_set_style_text_color(vol_lbl, p->text_muted, 0);
    lv_obj_set_style_text_font(vol_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(vol_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_set_vol_label = lv_label_create(vp);
    lv_obj_set_style_text_color(s_set_vol_label, p->accent, 0);
    lv_obj_set_style_text_font(s_set_vol_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_set_vol_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    settings_volume_label_update(audio_manager_get_volume());

    s_set_vol_slider = lv_slider_create(vp);
    lv_obj_set_size(s_set_vol_slider, 276, 8);
    lv_slider_set_range(s_set_vol_slider, AUDIO_MANAGER_VOLUME_MIN,
                        AUDIO_MANAGER_VOLUME_MAX);
    lv_slider_set_value(s_set_vol_slider, audio_manager_get_volume(), LV_ANIM_OFF);
    lv_obj_align(s_set_vol_slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_set_vol_slider, p->panel_alt, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_set_vol_slider, p->accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_set_vol_slider, p->accent, LV_PART_KNOB);
    /* Strip default-theme styles that trigger LVGL layer allocation:
     * - knob pad_all > 0 makes the knob overflow the track → layer needed
     * - grow/transform styles on PRESSED state animate transform_scale → layer
     * - shadow on knob → expensive soft-blur */
    lv_obj_set_style_pad_all(s_set_vol_slider, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s_set_vol_slider, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s_set_vol_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_width(s_set_vol_slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(s_set_vol_slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);

    lv_obj_add_event_cb(s_set_vol_slider, settings_volume_slider_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_set_vol_slider, settings_volume_slider_cb,
                        LV_EVENT_RELEASED, NULL);

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 304, 90);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_bg_color(list, p->panel, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, p->border, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    /* radius=0 + clip_corner=false: the default theme applies list_bg which sets
     * clip_corner=true.  clip_corner triggers a MASK_RECTANGLE draw task that
     * first clears the full area outside the rounded rect (top 150 px = 192 KB)
     * then applies per-row anti-aliased masking — together this exceeds the 5 s
     * task-watchdog timeout on this hardware.  Square corners are the fix. */
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_clip_corner(list, false, 0);

    /* List buttons: use panel_alt style but strip shadows — shadow blur on
     * multiple scrollable items causes LVGL rendering to take >5 s, triggering
     * the task watchdog and leaving the bottom half of the display unpainted. */
    /* Helper macro: style each list button.
     * The default LVGL theme applies list_item_grow (transform_width=PAD_DEF)
     * to every list button even in the DEFAULT state.  transform_width != 0
     * makes LVGL allocate a full-screen ARGB layer buffer in PSRAM before each
     * render — causing the task watchdog.  We override with 0 for all states. */
#define STYLE_LIST_BTN(btn)  do { \
    ui_theme_style_panel_alt(btn); \
    lv_obj_set_style_shadow_width((btn), 0, 0); \
    lv_obj_set_style_text_color((btn), p->text, 0); \
    lv_obj_set_style_pad_ver((btn), 4, 0); \
    lv_obj_set_style_transform_width((btn), 0, 0); \
    lv_obj_set_style_transform_width((btn), 0, LV_STATE_PRESSED); \
    lv_obj_set_style_transform_width((btn), 0, LV_STATE_FOCUS_KEY); \
    lv_obj_set_style_transform_height((btn), 0, 0); \
    lv_obj_set_style_transform_height((btn), 0, LV_STATE_PRESSED); \
    lv_obj_set_style_transform_height((btn), 0, LV_STATE_FOCUS_KEY); \
} while(0)

    lv_obj_t *btn_st = lv_list_add_btn(list, LV_SYMBOL_LIST, "Device Status");
    STYLE_LIST_BTN(btn_st);
    lv_obj_add_event_cb(btn_st, settings_status_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ble = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "BLE Debug");
    STYLE_LIST_BTN(btn_ble);
    lv_obj_add_event_cb(btn_ble, settings_ble_debug_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_audio = lv_list_add_btn(list, LV_SYMBOL_AUDIO, "Audio Debug");
    STYLE_LIST_BTN(btn_audio);
    lv_obj_add_event_cb(btn_audio, settings_debug_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_info = lv_list_add_btn(list, LV_SYMBOL_EDIT, "Info");
    STYLE_LIST_BTN(btn_info);
    lv_obj_add_event_cb(btn_info, settings_info_btn_cb, LV_EVENT_CLICKED, NULL);

#undef STYLE_LIST_BTN

    return scr;
}

void ui_screen_settings_on_show(void)
{
    settings_refresh_status();
    settings_refresh_volume();
}
