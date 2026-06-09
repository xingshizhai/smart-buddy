#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui/ui_manager.h"
#include "ui/ui_theme.h"
#include "ui/ui_fonts.h"
#include "ui/persona.h"
#include "buddy_hal/hal.h"

#define TAG "UI"

#ifndef CONFIG_UI_APPROVAL_TIMEOUT_S
#define CONFIG_UI_APPROVAL_TIMEOUT_S 30
#endif
#ifndef CONFIG_UI_SCREEN_OFF_TIMEOUT_S
#define CONFIG_UI_SCREEN_OFF_TIMEOUT_S 30
#endif
#define SCREEN_STACK_DEPTH 4

static ui_screen_id_t s_stack[SCREEN_STACK_DEPTH];
static int            s_stack_top = -1;
static lv_obj_t      *s_screens[UI_SCREEN_MAX] = {0};

/* Forward declarations for screen create functions */
static lv_obj_t *screen_boot_create(void);
lv_obj_t *screen_main_create(void);
lv_obj_t *screen_approval_create(void);
lv_obj_t *screen_status_create(void);
lv_obj_t *screen_settings_create(void);
/* Defined in separate source files */
lv_obj_t *screen_stats_create(void);
lv_obj_t *screen_info_create(void);
lv_obj_t *screen_menu_create(void);
lv_obj_t *screen_clock_create(void);
/* Defined in ui_screen_debug.c */
lv_obj_t *screen_debug_create(void);
void      ui_screen_debug_on_show(void);
void      ui_screen_debug_on_hide(void);

/* Defined in ui_screen_ble_debug.c */
lv_obj_t *screen_ble_debug_create(void);
void      ui_screen_ble_debug_on_show(void);
void      ui_screen_ble_debug_on_hide(void);

/* Screen-off control */
static uint8_t   s_saved_brightness = 100;
static bool      s_screen_dimmed    = false;
static lv_obj_t *s_wake_guard       = NULL;  /* fullscreen touch-catcher on lv_layer_top() */

static lv_timer_t *s_screenoff_timer = NULL;

extern void persona_frame_cb(const char *frame, void *ctx);

/* ── Wake-guard: transparent fullscreen object on lv_layer_top() ─────────
 * Created whenever the screen dims or goes dark.  Any touch wakes the
 * display and removes the guard; subsequent touches reach normal widgets.
 * ───────────────────────────────────────────────────────────────────────── */
static void wake_guard_cb(lv_event_t *e)
{
    (void)e;
    extern hal_handles_t g_hal;
    if (g_hal.display)
        g_hal.display->backlight_set(g_hal.display, s_saved_brightness);
    s_screen_dimmed = false;

    /* Reset the screen-off timer to its initial dim phase */
    if (s_screenoff_timer) {
        lv_timer_set_period(s_screenoff_timer, (uint32_t)CONFIG_UI_SCREEN_OFF_TIMEOUT_S * 1000);
        lv_timer_set_repeat_count(s_screenoff_timer, 1);
        lv_timer_reset(s_screenoff_timer);
    }

    /* Remove the guard — use async delete so we don't free the object while
     * still inside its own event callback (would corrupt the LVGL heap). */
    if (s_wake_guard) {
        lv_obj_delete_async(s_wake_guard);
        s_wake_guard = NULL;
    }
}

static void wake_guard_show(void)
{
    if (s_wake_guard) return;  /* already shown */
    lv_obj_t *guard = lv_obj_create(lv_layer_top());
    lv_obj_set_size(guard, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(guard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(guard, 0, 0);
    lv_obj_clear_flag(guard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(guard, wake_guard_cb, LV_EVENT_PRESSED, NULL);
    s_wake_guard = guard;
}

static void screenoff_timer_cb(lv_timer_t *t)
{
    /* Don't turn off during approval */
    if (ui_manager_current() == UI_SCREEN_APPROVAL) return;

    extern hal_handles_t g_hal;
    if (!g_hal.display) return;

    if (!s_screen_dimmed) {
        /* First timeout: dim to 20% */
        s_saved_brightness = 100; /* remember previous level */
        g_hal.display->backlight_set(g_hal.display, 20);
        s_screen_dimmed = true;
        wake_guard_show();
        /* Restart timer for next phase (full off) */
        if (s_screenoff_timer) {
            lv_timer_set_period(s_screenoff_timer,
                (uint32_t)CONFIG_UI_SCREEN_OFF_TIMEOUT_S * 1000);
            lv_timer_set_repeat_count(s_screenoff_timer, 1);
            lv_timer_resume(s_screenoff_timer);
        }
    } else {
        /* Second timeout: turn off completely */
        g_hal.display->backlight_set(g_hal.display, 0);
        /* Guard is already present from the dim phase */
    }
}

static lv_obj_t *screen_boot_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    const ui_palette_t *p = ui_theme_palette();
    ui_theme_style_root(scr);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 272, 126);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -6);
    ui_theme_style_panel(card);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "SMART BUDDY");
    lv_obj_set_style_text_color(title, p->accent, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Booting Runtime\nInitializing HAL + LVGL");
    lv_obj_set_style_text_color(sub, p->text_muted, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 14);

    lv_obj_t *pill = ui_theme_create_pill(scr, "ESP32-S3", p->text, p->panel_alt);
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -22);
    return scr;
}


static lv_obj_t *create_screen(ui_screen_id_t id)
{
    switch (id) {
    case UI_SCREEN_BOOT:     return screen_boot_create();
    case UI_SCREEN_MAIN:     return screen_main_create();
    case UI_SCREEN_APPROVAL: return screen_approval_create();
    case UI_SCREEN_STATUS:   return screen_status_create();
    case UI_SCREEN_SETTINGS: return screen_settings_create();
    case UI_SCREEN_DEBUG:     return screen_debug_create();
    case UI_SCREEN_BLE_DEBUG: return screen_ble_debug_create();
    case UI_SCREEN_STATS:    return screen_stats_create();
    case UI_SCREEN_INFO:     return screen_info_create();
    case UI_SCREEN_MENU:     return screen_menu_create();
    case UI_SCREEN_CLOCK:    return screen_clock_create();
    default:                 return NULL;
    }
}

static void notify_screen_lifecycle(ui_screen_id_t leaving, ui_screen_id_t entering)
{
    if (leaving == UI_SCREEN_DEBUG)       ui_screen_debug_on_hide();
    if (entering == UI_SCREEN_DEBUG)      ui_screen_debug_on_show();
    if (leaving == UI_SCREEN_BLE_DEBUG)   ui_screen_ble_debug_on_hide();
    if (entering == UI_SCREEN_BLE_DEBUG)  ui_screen_ble_debug_on_show();
    if (entering == UI_SCREEN_CLOCK)      ui_screen_clock_start();
    if (leaving == UI_SCREEN_CLOCK)       ui_screen_clock_stop();
    if (entering == UI_SCREEN_STATS)      ui_screen_stats_refresh();
    if (entering == UI_SCREEN_SETTINGS) ui_screen_settings_on_show();

    /* Wake screen from screen-off on any navigation */
    if (s_screen_dimmed) {
        extern hal_handles_t g_hal;
        if (g_hal.display)
            g_hal.display->backlight_set(g_hal.display, s_saved_brightness);
        s_screen_dimmed = false;
        if (s_wake_guard) { lv_obj_del(s_wake_guard); s_wake_guard = NULL; }
        /* Reset timer to dim phase */
        if (s_screenoff_timer) {
            lv_timer_set_period(s_screenoff_timer, (uint32_t)CONFIG_UI_SCREEN_OFF_TIMEOUT_S * 1000);
            lv_timer_set_repeat_count(s_screenoff_timer, 1);
            lv_timer_reset(s_screenoff_timer);
        }
    }
}

esp_err_t ui_manager_init(void)
{
    /* All LVGL object/timer creation must happen inside the port lock because
     * the LVGL task is already running by the time we get here. */
    if (!lvgl_port_lock(0)) return ESP_FAIL;

    for (int i = 0; i < UI_SCREEN_MAX; i++) {
        s_screens[i] = create_screen((ui_screen_id_t)i);
    }
    /* One-shot screen-off timer; reset on each activity.
     * Stage 1 (15s): dim to 20%. Stage 2 (30s): turn off completely. */
    s_screenoff_timer = lv_timer_create(screenoff_timer_cb, 15000, NULL);
    lv_timer_set_repeat_count(s_screenoff_timer, 1);

    persona_driver_init(persona_frame_cb, NULL);

    lvgl_port_unlock();
    return ESP_OK;
}

void ui_manager_deinit(void)
{
    persona_driver_deinit();
    for (int i = 0; i < UI_SCREEN_MAX; i++) {
        if (s_screens[i]) lv_obj_del(s_screens[i]);
    }
}

esp_err_t ui_manager_show(ui_screen_id_t id, ui_anim_type_t anim)
{
    if (id >= UI_SCREEN_MAX) return ESP_ERR_INVALID_ARG;
    ui_screen_id_t prev = ui_manager_current();
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[id],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON      :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT   :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT  :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        s_stack_top = 0;
        s_stack[0]  = id;
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, id);
    return ESP_OK;
}

esp_err_t ui_manager_push(ui_screen_id_t id, ui_anim_type_t anim)
{
    ui_screen_id_t prev = ui_manager_current();
    if (s_stack_top < SCREEN_STACK_DEPTH - 1) {
        s_stack[++s_stack_top] = id;
    }
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[id],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON      :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT   :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT  :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, id);
    return ESP_OK;
}

esp_err_t ui_manager_pop(ui_anim_type_t anim)
{
    ui_screen_id_t prev = ui_manager_current();
    if (s_stack_top > 0) s_stack_top--;
    ui_screen_id_t next = s_stack[s_stack_top];
    if (lvgl_port_lock(100)) {
        lv_scr_load_anim(s_screens[next],
                          anim == UI_ANIM_FADE       ? LV_SCR_LOAD_ANIM_FADE_ON      :
                          anim == UI_ANIM_SLIDE_LEFT  ? LV_SCR_LOAD_ANIM_MOVE_LEFT   :
                          anim == UI_ANIM_SLIDE_RIGHT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT  :
                                                        LV_SCR_LOAD_ANIM_NONE,
                          200, 0, false);
        lvgl_port_unlock();
    }
    notify_screen_lifecycle(prev, next);
    return ESP_OK;
}

ui_screen_id_t ui_manager_current(void)
{
    return s_stack_top >= 0 ? s_stack[s_stack_top] : UI_SCREEN_BOOT;
}

void ui_manager_on_state_change(sm_state_t new_state, sm_state_t old_state, void *ctx)
{
    extern hal_handles_t g_hal;

    switch (new_state) {
    case SM_STATE_SLEEP:
        ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_NONE);
        /* No transport connected — screen off immediately unless pairing passkey is active. */
        if (lvgl_port_lock(100)) {
            if (s_screenoff_timer) lv_timer_pause(s_screenoff_timer);
            lvgl_port_unlock();
        }
        if (g_hal.display) {
            g_hal.display->backlight_set(g_hal.display,
                                         ui_screen_main_has_passkey() ? 100 : 0);
        }
        if (g_hal.led)     g_hal.led->off(g_hal.led);
        break;

    case SM_STATE_ATTENTION:
        ui_manager_push(UI_SCREEN_APPROVAL, UI_ANIM_SLIDE_LEFT);
        /* Keep screen on, pause inactivity timer during approval */
        if (lvgl_port_lock(100)) {
            if (s_screenoff_timer) lv_timer_pause(s_screenoff_timer);
            lvgl_port_unlock();
        }
        if (g_hal.display) g_hal.display->backlight_set(g_hal.display, 100);
        /* Red alert blink */
        if (g_hal.led) {
            g_hal.led->set_rgb(g_hal.led, 255, 0, 0);
            g_hal.led->blink(g_hal.led, 200, 200, -1);
        }
        break;

    default:
        if (old_state == SM_STATE_ATTENTION) {
            ui_manager_pop(UI_ANIM_SLIDE_RIGHT);
        }
        /* Screen on; reset inactivity timer — screen stays on while connected */
        if (g_hal.display) g_hal.display->backlight_set(g_hal.display, 100);
        s_screen_dimmed = false;
        if (lvgl_port_lock(100)) {
            if (s_screenoff_timer) {
                lv_timer_set_period(s_screenoff_timer, 15000);
                lv_timer_set_repeat_count(s_screenoff_timer, 1);
                lv_timer_pause(s_screenoff_timer);
            }
            lvgl_port_unlock();
        }
        /* LED color per state */
        if (g_hal.led) {
            switch (new_state) {
            case SM_STATE_IDLE:
                g_hal.led->set_rgb(g_hal.led, 0, 150, 50);
                g_hal.led->blink(g_hal.led, 1200, 1200, -1);
                break;
            case SM_STATE_BUSY:
                g_hal.led->set_rgb(g_hal.led, 0, 100, 220);
                g_hal.led->blink(g_hal.led, 400, 400, -1);
                break;
            case SM_STATE_CELEBRATE:
                g_hal.led->set_rgb(g_hal.led, 255, 200, 0);
                g_hal.led->blink(g_hal.led, 150, 150, -1);
                break;
            case SM_STATE_DIZZY:
                g_hal.led->set_rgb(g_hal.led, 255, 100, 0);
                g_hal.led->blink(g_hal.led, 100, 100, -1);
                break;
            case SM_STATE_HEART:
                g_hal.led->set_rgb(g_hal.led, 255, 50, 100);
                g_hal.led->blink(g_hal.led, 700, 700, -1);
                break;
            default:
                g_hal.led->off(g_hal.led);
                break;
            }
        }
        break;
    }

    ui_screen_main_set_state(new_state);
}
