#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "ui/ui_fonts.h"
#include "ui/persona.h"
#include "state_machine.h"

#define MAIN_STATUS_H  28

static lv_obj_t *s_main_title_label   = NULL;
static lv_obj_t *s_persona_label      = NULL;
static lv_obj_t *s_main_content_label = NULL;
static lv_obj_t *s_ble_indicator      = NULL;
static lv_obj_t *s_passkey_label      = NULL;

static lv_obj_t *s_transcript_area    = NULL;
static lv_obj_t *s_transcript_label   = NULL;
static bool      s_transcript_visible  = true;

void persona_frame_cb(const char *frame, void *ctx)
{
    (void)ctx;
    if (s_persona_label)
        lv_label_set_text(s_persona_label, frame);
}

static void main_settings_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_manager_push(UI_SCREEN_SETTINGS, UI_ANIM_SLIDE_LEFT);
}

lv_obj_t *screen_main_create(void)
{
    const ui_palette_t *p = ui_theme_palette();
    lv_obj_t *scr = lv_obj_create(NULL);
    ui_theme_style_root(scr);

    /* ── Title bar ────────────────────────────────────────────────── */
    lv_obj_t *bar = ui_theme_create_title_bar(scr, "SmartBuddy");

    /* Title text: dynamic service summary */
    s_main_title_label = lv_label_create(bar);
    lv_label_set_text(s_main_title_label, "Ready");
    lv_obj_set_style_text_color(s_main_title_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_main_title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_main_title_label, 75);
    lv_label_set_long_mode(s_main_title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(s_main_title_label, LV_ALIGN_LEFT_MID, 12, 0);

    /* BLE indicator — right side of title bar */
    s_ble_indicator = lv_label_create(bar);
    lv_label_set_text(s_ble_indicator, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_ble_indicator, p->text_muted, 0);
    lv_obj_set_style_text_font(s_ble_indicator, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ble_indicator, LV_ALIGN_RIGHT_MID, -10, 0);

    /* Settings gear icon — left of BLE, tap to open settings */
    lv_obj_t *gear = lv_label_create(bar);
    lv_label_set_text(gear, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gear, p->text_muted, 0);
    lv_obj_set_style_text_font(gear, &lv_font_montserrat_14, 0);
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(gear, 8);
    lv_obj_add_event_cb(gear, main_settings_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hero = lv_obj_create(scr);
    lv_obj_set_size(hero, 304, 132);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 46);
    ui_theme_style_panel(hero);

    /* ── Persona ASCII art label — centered, monospace font ─────── */
    s_persona_label = lv_label_create(hero);
    lv_label_set_text(s_persona_label, "");
    lv_obj_set_style_text_color(s_persona_label, p->text, 0);
    lv_obj_set_style_text_font(s_persona_label, &lv_font_unscii_8, 0);
    lv_obj_set_size(s_persona_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_persona_label, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_align(s_persona_label, LV_TEXT_ALIGN_LEFT, 0);

    /* ── Content / msg label — centered below state ──────────────── */
    s_main_content_label = lv_label_create(hero);
    lv_label_set_text(s_main_content_label, "Connect via Hardware Buddy");
    lv_obj_set_style_text_color(s_main_content_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_main_content_label, &g_font_14_cjk, 0);
    lv_obj_set_width(s_main_content_label, 272);
    lv_label_set_long_mode(s_main_content_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_main_content_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_align(s_main_content_label, LV_TEXT_ALIGN_CENTER, 0);

    /* ── Passkey overlay (hidden by default, shown during BLE pairing) ─ */
    s_passkey_label = lv_label_create(scr);
    lv_label_set_text(s_passkey_label, "");
    lv_obj_set_style_text_color(s_passkey_label, p->warning, 0);
    lv_obj_set_style_text_font(s_passkey_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(s_passkey_label, p->bg, 0);
    lv_obj_set_style_bg_opa(s_passkey_label, LV_OPA_90, 0);
    lv_obj_set_size(s_passkey_label, 320, 240);
    lv_obj_align(s_passkey_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(s_passkey_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);

    /* ── HUD transcript area (bottom of screen) ────────────────────── */
    s_transcript_area = lv_obj_create(scr);
    lv_obj_set_size(s_transcript_area, 304, 58);
    lv_obj_align(s_transcript_area, LV_ALIGN_BOTTOM_MID, 0, -6);
    ui_theme_style_panel_alt(s_transcript_area);

    s_transcript_label = lv_label_create(s_transcript_area);
    lv_label_set_text(s_transcript_label, "");
    lv_obj_set_style_text_color(s_transcript_label, p->text_muted, 0);
    lv_obj_set_style_text_font(s_transcript_label, &g_font_14_cjk, 0);
    lv_obj_set_width(s_transcript_label, 286);
    lv_label_set_long_mode(s_transcript_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_transcript_label, LV_ALIGN_TOP_LEFT, 2, 2);

    return scr;
}

bool ui_screen_main_has_passkey(void)
{
    if (!s_passkey_label) return false;
    return !lv_obj_has_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);
}

void ui_screen_main_set_state(sm_state_t state)
{
    if (state >= SM_STATE_MAX) return;
    if (!lvgl_port_lock(100)) return;
    if (s_persona_label) {
        lv_color_t c;
        switch (state) {
        case SM_STATE_IDLE:      c = lv_color_make(0x00, 0xCC, 0x44); break;
        case SM_STATE_BUSY:      c = lv_color_make(0x44, 0xBB, 0xFF); break;
        case SM_STATE_ATTENTION: c = lv_color_make(0xFF, 0x88, 0x00); break;
        case SM_STATE_CELEBRATE: c = lv_color_make(0xFF, 0xFF, 0x44); break;
        case SM_STATE_DIZZY:     c = lv_color_make(0xFF, 0x66, 0x88); break;
        case SM_STATE_HEART:     c = lv_color_make(0xFF, 0x44, 0x66); break;
        default:                 c = lv_color_make(0x55, 0x55, 0x55); break;
        }
        lv_obj_set_style_text_color(s_persona_label, c, 0);
    }
    persona_driver_set_state(state);
    lvgl_port_unlock();
}

void ui_screen_main_set_token_count(uint32_t tokens)
{
    if (!lvgl_port_lock(100)) return;
    if (s_main_title_label) {
        char buf[24];
        if (tokens >= 100000)
            snprintf(buf, sizeof(buf), "T: %luk", (unsigned long)(tokens / 1000));
        else if (tokens >= 10000)
            snprintf(buf, sizeof(buf), "T: %.1fk", (float)tokens / 1000.0f);
        else
            snprintf(buf, sizeof(buf), "T: %lu", (unsigned long)tokens);
        lv_label_set_text(s_main_title_label, buf);
    }
    lvgl_port_unlock();
}

void ui_screen_main_set_ble_connected(bool connected)
{
    const ui_palette_t *p = ui_theme_palette();
    if (lvgl_port_lock(100)) {
        if (s_ble_indicator) {
            lv_color_t color = connected ? p->success : p->text_muted;
            lv_obj_set_style_text_color(s_ble_indicator, color, 0);
        }
        lvgl_port_unlock();
    }
}

void ui_screen_main_set_passkey(uint32_t passkey)
{
    if (!lvgl_port_lock(100)) return;
    if (s_passkey_label) {
        if (passkey > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "BLE Pairing\nEnter on desktop:\n\n%06lu", (unsigned long)passkey);
            lv_label_set_text(s_passkey_label, buf);
            lv_obj_clear_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_passkey_label, "");
            lv_obj_add_flag(s_passkey_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lvgl_port_unlock();
}

void ui_screen_main_set_msg(const char *msg)
{
    const ui_palette_t *p = ui_theme_palette();
    if (!msg) msg = "";
    if (!lvgl_port_lock(100)) return;
    if (s_main_content_label) {
        lv_label_set_text(s_main_content_label, msg);
        bool active = msg[0] && strcmp(msg, "ready") != 0;
        lv_color_t c = active ? p->warning : p->text_muted;
        lv_obj_set_style_text_color(s_main_content_label, c, 0);
    }
    lvgl_port_unlock();
}

void ui_screen_main_set_entries(const char (*entries)[92], uint8_t n)
{
    if (!lvgl_port_lock(100)) return;

    if (s_transcript_area) {
        if (s_transcript_visible && n > 0) {
            char buf[512] = {0};
            size_t pos = 0;
            for (uint8_t i = 0; i < n && i < 8; i++) {
                int written = snprintf(buf + pos, sizeof(buf) - pos,
                                       "%s\n", entries[i]);
                if (written > 0 && pos + written < sizeof(buf))
                    pos += (size_t)written;
            }
            if (s_transcript_label)
                lv_label_set_text(s_transcript_label, buf);
            lv_obj_clear_flag(s_transcript_area, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_transcript_area, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lvgl_port_unlock();
}
