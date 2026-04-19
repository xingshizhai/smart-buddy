#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "transport/transport.h"
#include "ui/ui_manager.h"

#define TAG      "UI_BLE_DBG"
#define SCR_W    320
#define SCR_H    240
#define TITLE_H   30
#define LOG_LINES  6
#define LOG_LINE_H 18

/* ── State ────────────────────────────────────────────────────────────── */
static lv_obj_t  *s_scr          = NULL;
static lv_obj_t  *s_lbl_status   = NULL;
static lv_obj_t  *s_lbl_mac      = NULL;
static lv_obj_t  *s_lbl_mtu      = NULL;
static lv_obj_t  *s_log_labels[LOG_LINES];
static lv_timer_t *s_timer        = NULL;

/* Circular log buffer */
static char s_log_buf[LOG_LINES][64];
static int  s_log_next = 0;

/* ── Internal helpers ─────────────────────────────────────────────────── */

static void ble_log_push(const char *line)
{
    strlcpy(s_log_buf[s_log_next], line, sizeof(s_log_buf[0]));
    s_log_next = (s_log_next + 1) % LOG_LINES;
}

static void refresh_log_labels(void)
{
    for (int i = 0; i < LOG_LINES; i++) {
        int idx = (s_log_next + i) % LOG_LINES;
        lv_label_set_text(s_log_labels[i], s_log_buf[idx]);
    }
}

/* ── LVGL update timer (500 ms) ───────────────────────────────────────── */

static void update_timer_cb(lv_timer_t *t)
{
    if (!s_scr) return;

    transport_state_t st = transport_get_state(TRANSPORT_ID_BLE);
    const char *st_str;
    lv_color_t  st_col;
    if (st == TRANSPORT_STATE_CONNECTED) {
        st_str = "#00cc44 CONNECTED#";
        st_col = lv_color_make(0x00, 0xCC, 0x44);
    } else {
        st_str = "#888888 ADVERTISING#";
        st_col = lv_color_make(0x88, 0x88, 0x88);
    }
    (void)st_col;
    lv_label_set_text(s_lbl_status, st_str);
    refresh_log_labels();
}

/* ── Button callbacks ─────────────────────────────────────────────────── */

static void back_btn_cb(lv_event_t *e)
{
    if (lvgl_port_lock(50)) {
        if (s_timer) lv_timer_pause(s_timer);
        lvgl_port_unlock();
    }
    ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
}

/* Send a minimal heartbeat-ack and show it in the log */
static void send_ack_btn_cb(lv_event_t *e)
{
    const char *msg = "{\"type\":\"ack\"}\n";
    esp_err_t r = transport_send_all((const uint8_t *)msg, strlen(msg));
    char line[64];
    snprintf(line, sizeof(line), "TX: %s  [%s]",
             "{\"type\":\"ack\"}",
             r == ESP_OK ? "OK" : esp_err_to_name(r));
    ble_log_push(line);
    ESP_LOGI(TAG, "%s", line);
}

/* Send a mock heartbeat to test session_update decode */
static void send_hb_btn_cb(lv_event_t *e)
{
    const char *msg =
        "{\"total\":1,\"running\":1,\"waiting\":0,"
        "\"tokens\":1000,\"tokens_today\":500}\n";
    esp_err_t r = transport_send_all((const uint8_t *)msg, strlen(msg));
    char line[64];
    snprintf(line, sizeof(line), "TX: heartbeat  [%s]",
             r == ESP_OK ? "OK" : esp_err_to_name(r));
    ble_log_push(line);
    ESP_LOGI(TAG, "%s", line);
}

/* Send a mock approval-request prompt to test the approval screen */
static void send_prompt_btn_cb(lv_event_t *e)
{
    const char *msg =
        "{\"total\":1,\"running\":0,\"waiting\":1,"
        "\"tokens\":1000,\"tokens_today\":500,"
        "\"prompt\":{\"id\":\"test-01\","
        "\"tool\":\"bash\","
        "\"hint\":\"ls /tmp\"}}\n";
    esp_err_t r = transport_send_all((const uint8_t *)msg, strlen(msg));
    char line[64];
    snprintf(line, sizeof(line), "TX: prompt  [%s]",
             r == ESP_OK ? "OK" : esp_err_to_name(r));
    ble_log_push(line);
    ESP_LOGI(TAG, "%s", line);
}

/* ── Screen create ────────────────────────────────────────────────────── */

static lv_obj_t *make_small_btn(lv_obj_t *parent, int x, int y,
                                  int w, int h,
                                  lv_color_t col,
                                  const char *label_txt,
                                  lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(btn, col, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    return btn;
}

lv_obj_t *screen_ble_debug_create(void)
{
    memset(s_log_buf, 0, sizeof(s_log_buf));
    s_log_next = 0;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x04, 0x08, 0x14), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    s_scr = scr;

    /* ── Title bar ──────────────────────────────────────────────────── */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, SCR_W, TITLE_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x10, 0x18, 0x30), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(bar);
    lv_obj_set_size(btn_back, 50, 22);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(0x20, 0x30, 0x55), 0);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_back);

    lv_obj_t *lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "BLE Debug");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* Status dot — right of title bar */
    s_lbl_status = lv_label_create(bar);
    lv_label_set_text(s_lbl_status, "#888888 ●#");
    lv_label_set_recolor(s_lbl_status, true);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_RIGHT_MID, -4, 0);

    /* ── Info rows ──────────────────────────────────────────────────── */
    int row_y = TITLE_H + 2;

    /* MAC address (populated on_show after BLE stack is up) */
    s_lbl_mac = lv_label_create(scr);
    lv_label_set_text(s_lbl_mac, "MAC: --");
    lv_obj_set_style_text_color(s_lbl_mac, lv_color_make(0xAA, 0xCC, 0xFF), 0);
    lv_obj_align(s_lbl_mac, LV_ALIGN_TOP_LEFT, 8, row_y);
    row_y += 20;

    s_lbl_mtu = lv_label_create(scr);
    lv_label_set_text(s_lbl_mtu, "MTU: --");
    lv_obj_set_style_text_color(s_lbl_mtu, lv_color_make(0x88, 0xAA, 0xCC), 0);
    lv_obj_align(s_lbl_mtu, LV_ALIGN_TOP_LEFT, 8, row_y);
    row_y += 22;

    /* ── Log area (6 lines, monospace-ish) ──────────────────────────── */
    lv_obj_t *log_bg = lv_obj_create(scr);
    lv_obj_set_size(log_bg, SCR_W, LOG_LINES * LOG_LINE_H + 6);
    lv_obj_align(log_bg, LV_ALIGN_TOP_LEFT, 0, row_y);
    lv_obj_set_style_bg_color(log_bg, lv_color_make(0x02, 0x04, 0x0A), 0);
    lv_obj_set_style_border_color(log_bg, lv_color_make(0x20, 0x30, 0x55), 0);
    lv_obj_set_style_border_width(log_bg, 1, 0);
    lv_obj_set_style_pad_all(log_bg, 3, 0);
    lv_obj_clear_flag(log_bg, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < LOG_LINES; i++) {
        s_log_labels[i] = lv_label_create(log_bg);
        lv_label_set_text(s_log_labels[i], "");
        lv_obj_set_style_text_color(s_log_labels[i], lv_color_make(0x55, 0xCC, 0x88), 0);
        lv_obj_align(s_log_labels[i], LV_ALIGN_TOP_LEFT, 0, i * LOG_LINE_H);
    }
    row_y += LOG_LINES * LOG_LINE_H + 10;

    /* ── Button row (3 buttons) ─────────────────────────────────────── */
    int rem_h   = SCR_H - row_y - 4;
    int btn_w   = SCR_W / 3 - 4;

    make_small_btn(scr, 2,              row_y, btn_w, rem_h,
                   lv_color_make(0x11, 0x44, 0x77), "ACK",    send_ack_btn_cb);
    make_small_btn(scr, btn_w + 4,      row_y, btn_w, rem_h,
                   lv_color_make(0x11, 0x66, 0x33), "HB",     send_hb_btn_cb);
    make_small_btn(scr, (btn_w + 2) * 2, row_y, btn_w, rem_h,
                   lv_color_make(0x66, 0x33, 0x11), "PROMPT", send_prompt_btn_cb);

    /* ── Periodic update timer ──────────────────────────────────────── */
    s_timer = lv_timer_create(update_timer_cb, 500, NULL);
    lv_timer_pause(s_timer);

    ESP_LOGI(TAG, "BLE debug screen created");
    return scr;
}

void ui_screen_ble_debug_on_show(void)
{
    if (lvgl_port_lock(50)) {
        /* Refresh MAC (BLE stack is up by the time the screen is shown) */
        uint8_t mac[6] = {0};
        char mac_str[32];
        if (transport_ble_get_mac(mac) == ESP_OK)
            snprintf(mac_str, sizeof(mac_str), "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        else
            strlcpy(mac_str, "MAC: N/A", sizeof(mac_str));
        lv_label_set_text(s_lbl_mac, mac_str);

        /* Refresh MTU */
        char mtu_buf[24];
        snprintf(mtu_buf, sizeof(mtu_buf), "MTU: %u", transport_ble_get_mtu());
        lv_label_set_text(s_lbl_mtu, mtu_buf);

        if (s_timer) lv_timer_resume(s_timer);
        lvgl_port_unlock();
    }
}

void ui_screen_ble_debug_on_hide(void)
{
    if (lvgl_port_lock(50)) {
        if (s_timer) lv_timer_pause(s_timer);
        lvgl_port_unlock();
    }
}
