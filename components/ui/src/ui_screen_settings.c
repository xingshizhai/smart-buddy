#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "audio_manager.h"
#include "transport/transport.h"

static lv_obj_t *s_set_ble_val    = NULL;
static lv_obj_t *s_set_wifi_val   = NULL;
static lv_obj_t *s_set_usb_val    = NULL;
static lv_obj_t *s_set_vol_slider = NULL;
static lv_obj_t *s_set_vol_label  = NULL;

/* ── helpers ────────────────────────────────────────────────────────────── */

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
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr)
                snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
            else
                strlcpy(buf, "No IP", sizeof(buf));
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

/* ── button callbacks ───────────────────────────────────────────────────── */

static void settings_back_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_pop(UI_ANIM_NONE);
}

static void settings_status_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_screen_status_refresh();
    ui_manager_push(UI_SCREEN_STATUS, UI_ANIM_SLIDE_LEFT);
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

static void settings_info_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_screen_info_reset();
    ui_manager_push(UI_SCREEN_INFO, UI_ANIM_SLIDE_LEFT);
}

/* ── screen builder ─────────────────────────────────────────────────────── */

/*
 * Page structure  (320 × 240 px)
 * ─────────────────────────────────────────────────────────────────────────
 *  Title bar (fixed, h=36)
 *  Scroll container  y=36, h=204 — transparent, vertical-scroll
 *
 *   y=  4  h=32   BLE status card   ─┐
 *   y= 40  h=32   WiFi status card   ├ connection group (3 separate cards)
 *   y= 76  h=32   USB status card   ─┘
 *   y=116  h=50   Alert Volume card  (label + 12 px gap + slider)
 *   y=174  h=120  Navigation card    (4 buttons, no internal scroll)
 *
 *  Content height ≈ 302 px → 98 px overflow → page scrolls ~half screen
 * ─────────────────────────────────────────────────────────────────────────
 */

/* Uniform card style for every panel on this page.
 * No shadow, no clip_corner, radius=0 — see comment block in previous
 * version for the full rendering-constraint rationale.               */
#define SECTION_PANEL(obj, _pad) do { \
    lv_obj_set_style_bg_color((obj), p->panel, 0); \
    lv_obj_set_style_bg_opa((obj), LV_OPA_COVER, 0); \
    lv_obj_set_style_border_color((obj), p->border, 0); \
    lv_obj_set_style_border_width((obj), 1, 0); \
    lv_obj_set_style_radius((obj), 0, 0); \
    lv_obj_set_style_shadow_width((obj), 0, 0); \
    lv_obj_set_style_clip_corner((obj), false, 0); \
    lv_obj_set_style_pad_all((obj), (_pad), 0); \
    lv_obj_clear_flag((obj), LV_OBJ_FLAG_SCROLLABLE); \
} while(0)

/* Single-row status card: icon + key on the left, live value on the right.
 * h=32, pad=8 → inner 16 px (font 14 px, 2 px breathing room).        */
#define STATUS_CARD(cont, y_off, sym, key, val_ptr) do { \
    lv_obj_t *_c = lv_obj_create(cont); \
    lv_obj_set_size(_c, 304, 32); \
    lv_obj_align(_c, LV_ALIGN_TOP_MID, 0, (y_off)); \
    SECTION_PANEL(_c, 8); \
    lv_obj_t *_k = lv_label_create(_c); \
    lv_label_set_text(_k, sym "  " key); \
    lv_obj_set_style_text_color(_k, p->text_muted, 0); \
    lv_obj_set_style_text_font(_k, &lv_font_montserrat_14, 0); \
    lv_obj_align(_k, LV_ALIGN_LEFT_MID, 0, 0); \
    (val_ptr) = lv_label_create(_c); \
    lv_label_set_text(val_ptr, "--"); \
    lv_obj_set_style_text_color(val_ptr, p->text_muted, 0); \
    lv_obj_set_style_text_font(val_ptr, &lv_font_montserrat_14, 0); \
    lv_obj_set_width(val_ptr, 196); \
    lv_label_set_long_mode(val_ptr, LV_LABEL_LONG_DOT); \
    lv_obj_align(val_ptr, LV_ALIGN_RIGHT_MID, 0, 0); \
} while(0)

/* Nav-button style: suppresses shadow and default-theme transforms that
 * trigger full-screen ARGB layer allocation on this hardware.         */
#define NAV_BTN(btn) do { \
    lv_obj_set_style_bg_color((btn), p->panel_alt, 0); \
    lv_obj_set_style_bg_opa((btn), LV_OPA_COVER, 0); \
    lv_obj_set_style_border_width((btn), 0, 0); \
    lv_obj_set_style_shadow_width((btn), 0, 0); \
    lv_obj_set_style_radius((btn), 0, 0); \
    lv_obj_set_style_pad_ver((btn), 5, 0); \
    lv_obj_set_style_pad_hor((btn), 6, 0); \
    lv_obj_set_style_text_color((btn), p->text, 0); \
    lv_obj_set_style_text_font((btn), &lv_font_montserrat_14, 0); \
    lv_obj_set_style_transform_width((btn), 0, 0); \
    lv_obj_set_style_transform_width((btn), 0, LV_STATE_PRESSED); \
    lv_obj_set_style_transform_width((btn), 0, LV_STATE_FOCUS_KEY); \
    lv_obj_set_style_transform_height((btn), 0, 0); \
    lv_obj_set_style_transform_height((btn), 0, LV_STATE_PRESSED); \
    lv_obj_set_style_transform_height((btn), 0, LV_STATE_FOCUS_KEY); \
} while(0)

lv_obj_t *screen_settings_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    /* ── Fixed title bar ──────────────────────────────────────────────── */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "Settings");
    ui_theme_create_back_button(bar, settings_back_btn_cb);

    /* ── Scrollable content container ────────────────────────────────── */
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 320, 204);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_opa(cont,       LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0,             0);
    lv_obj_set_style_pad_all(cont,      0,             0);
    lv_obj_set_style_radius(cont,       0,             0);
    lv_obj_set_style_clip_corner(cont,  false,         0);
    lv_obj_set_style_shadow_width(cont, 0,             0);
    lv_obj_set_scroll_dir(cont,         LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont,     LV_SCROLLBAR_MODE_AUTO);

    /* ── Three individual connection status cards ─────────────────────── */
    STATUS_CARD(cont,  4, LV_SYMBOL_BLUETOOTH, "BLE",  s_set_ble_val);
    STATUS_CARD(cont, 40, LV_SYMBOL_WIFI,      "WiFi", s_set_wifi_val);
    STATUS_CARD(cont, 76, LV_SYMBOL_USB,       "USB",  s_set_usb_val);

#undef STATUS_CARD

    /* ── Alert Volume card  (y=116, h=50) ────────────────────────────
     *  pad_all=8 → inner 34 px
     *  "🔊 Alert Volume" label (14 px) + 12 px gap + slider (8 px)    */
    lv_obj_t *vp = lv_obj_create(cont);
    lv_obj_set_size(vp, 304, 50);
    lv_obj_align(vp, LV_ALIGN_TOP_MID, 0, 116);
    SECTION_PANEL(vp, 8);

    lv_obj_t *vol_lbl = lv_label_create(vp);
    lv_label_set_text(vol_lbl, LV_SYMBOL_VOLUME_MAX "  Alert Volume");
    lv_obj_set_style_text_color(vol_lbl, p->text_muted, 0);
    lv_obj_set_style_text_font(vol_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(vol_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_set_vol_label = lv_label_create(vp);
    lv_obj_set_style_text_color(s_set_vol_label, p->accent, 0);
    lv_obj_set_style_text_font(s_set_vol_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_set_vol_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    settings_volume_label_update(audio_manager_get_volume());

    s_set_vol_slider = lv_slider_create(vp);
    lv_obj_set_size(s_set_vol_slider, 272, 8);
    lv_slider_set_range(s_set_vol_slider,
                        AUDIO_MANAGER_VOLUME_MIN, AUDIO_MANAGER_VOLUME_MAX);
    lv_slider_set_value(s_set_vol_slider, audio_manager_get_volume(), LV_ANIM_OFF);
    lv_obj_align(s_set_vol_slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_set_vol_slider, p->panel_alt, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_set_vol_slider, p->accent,    LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_set_vol_slider, p->accent,    LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_set_vol_slider,       0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s_set_vol_slider,  0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s_set_vol_slider,  0, LV_PART_MAIN);
    lv_obj_set_style_transform_width(s_set_vol_slider,  0, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(s_set_vol_slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_set_vol_slider, settings_volume_slider_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_set_vol_slider, settings_volume_slider_cb,
                        LV_EVENT_RELEASED, NULL);

    /* ── Navigation card  (y=174, h=120) ─────────────────────────────
     *  4 buttons × 24 px + 3 gaps × 4 px = 108 px inner,  pad_all=6
     *  No internal scroll — all 4 buttons fit exactly.                */
    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_size(list, 304, 120);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 174);
    lv_obj_set_style_bg_color(list,     p->panel,     0);
    lv_obj_set_style_bg_opa(list,       LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, p->border,    0);
    lv_obj_set_style_border_width(list, 1,            0);
    lv_obj_set_style_radius(list,       0,            0);
    lv_obj_set_style_clip_corner(list,  false,        0);
    lv_obj_set_style_shadow_width(list, 0,            0);
    lv_obj_set_style_pad_all(list,      6,            0);
    lv_obj_set_style_pad_row(list,      4,            0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_st = lv_list_add_btn(list, LV_SYMBOL_LIST,      "Device Status");
    NAV_BTN(btn_st);
    lv_obj_add_event_cb(btn_st,    settings_status_btn_cb,    LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ble = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "BLE Debug");
    NAV_BTN(btn_ble);
    lv_obj_add_event_cb(btn_ble,   settings_ble_debug_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_audio = lv_list_add_btn(list, LV_SYMBOL_AUDIO,  "Audio Debug");
    NAV_BTN(btn_audio);
    lv_obj_add_event_cb(btn_audio, settings_debug_btn_cb,     LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_info = lv_list_add_btn(list, LV_SYMBOL_EDIT,    "Info");
    NAV_BTN(btn_info);
    lv_obj_add_event_cb(btn_info,  settings_info_btn_cb,      LV_EVENT_CLICKED, NULL);

#undef NAV_BTN
#undef SECTION_PANEL

    return scr;
}

void ui_screen_settings_on_show(void)
{
    settings_refresh_status();
    settings_refresh_volume();
}
