#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "buddy_hal/hal.h"
#include "buddy_hal/agent_events.h"
#include "state_machine.h"
#include "transport/transport.h"
#include "protocol/protocol.h"
#include "agent_core.h"
#include "agent_stats.h"
#include "imu_monitor.h"
#include "audio_manager.h"
#include "ui/ui_manager.h"
#include "ui/ui_fonts.h"
#include "ui/ui_button_router.h"
#include "app_config.h"
#include "app_notify.h"
#include "wifi_manager.h"
#include "debug_screenshot.h"

#define TAG "MAIN"

#ifndef CONFIG_PROTO_CLAUDE_BUDDY_ENABLED
#define CONFIG_PROTO_CLAUDE_BUDDY_ENABLED 0
#endif
#ifndef CONFIG_PROTO_OPENCLAW_ENABLED
#define CONFIG_PROTO_OPENCLAW_ENABLED 0
#endif
#ifndef CONFIG_PROTO_HERMES_ENABLED
#define CONFIG_PROTO_HERMES_ENABLED 0
#endif
#ifndef CONFIG_TRANSPORT_BLE_ENABLED
#define CONFIG_TRANSPORT_BLE_ENABLED 1
#endif
#ifndef CONFIG_TRANSPORT_WS_ENABLED
#define CONFIG_TRANSPORT_WS_ENABLED 0
#endif
#ifndef CONFIG_TRANSPORT_USB_ENABLED
#define CONFIG_TRANSPORT_USB_ENABLED 1
#endif

static sm_handle_t s_sm = NULL;

/* LEFT long-press handler: open pet stats */
static void left_long_press_cb(hal_button_id_t id, hal_button_event_t evt, void *ctx)
{
    (void)id; (void)evt; (void)ctx;
    ui_button_router_handle(BTN_ACTION_LEFT_LONG);
}

static void app_sm_callback(sm_state_t new_state, sm_state_t old_state, void *ctx)
{
    app_notify_on_state_change(new_state, old_state);
    ui_manager_on_state_change(new_state, old_state, ctx);
}

/* ── Push-to-talk ────────────────────────────────────────────────────── */

static void ptt_button_cb(hal_button_id_t id, hal_button_event_t evt, void *ctx)
{
    /* During ATTENTION state, BOOT acts as B-key (Deny) */
    if (sm_get_state(s_sm) == SM_STATE_ATTENTION) {
        if (evt == HAL_BTN_EVT_PRESS_DOWN)
            ui_approval_handle_key(false);
        return;
    }

    agent_event_t agent_evt = { .timestamp_us = esp_timer_get_time() };
    if (evt == HAL_BTN_EVT_PRESS_DOWN) {
        audio_manager_record_start();
        agent_evt.type = AGENT_EVT_AUDIO_RECORD_START;
        agent_core_post_event(&agent_evt);
        ESP_LOGI("PTT", "recording started");
    } else if (evt == HAL_BTN_EVT_PRESS_UP) {
        audio_manager_record_stop();
        agent_evt.type = AGENT_EVT_AUDIO_RECORD_STOP;
        agent_core_post_event(&agent_evt);
        ESP_LOGI("PTT", "recording stopped");
    }
}

/* A-key / BOOT long-press: Approve in ATTENTION; open menu otherwise */
static void approve_button_cb(hal_button_id_t id, hal_button_event_t evt, void *ctx)
{
    if (sm_get_state(s_sm) == SM_STATE_ATTENTION) {
        ui_approval_handle_key(true);
        return;
    }
    /* IDLE or any non-ATTENTION state: open menu overlay */
    ui_button_router_handle(BTN_ACTION_CENTER_LONG);
    ESP_LOGI("BTN", "menu opened (state=%d)", sm_get_state(s_sm));
}

/* LEFT (mute): deny during ATTENTION; back/stats otherwise */
static void left_button_cb(hal_button_id_t id, hal_button_event_t evt, void *ctx)
{
    (void)id; (void)evt; (void)ctx;
    if (sm_get_state(s_sm) == SM_STATE_ATTENTION) {
        ui_approval_handle_key(false);  /* Deny */
        return;
    }
    ui_button_router_handle(BTN_ACTION_LEFT_SHORT);
}

/* ── Periodic tasks ─────────────────────────────────────────────────── */

static void heartbeat_task(void *arg)
{
    uint32_t last_passkey = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));

        /* Poll passkey for pairing screen display (always, even without proto) */
        uint32_t pk = transport_ble_get_passkey();
        if (pk != last_passkey) {
            last_passkey = pk;
            ui_screen_main_set_passkey(pk);
            if (pk > 0) {
                ESP_LOGI(TAG, "=== BLE PAIRING: enter passkey %06lu on desktop ===",
                         (unsigned long)pk);
            }
        }

        /* Heartbeat ACK is sent by agent_core only after receiving
         * a desktop heartbeat snapshot. Do not send unsolicited ACKs here. */
    }
}

static void stats_persist_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATS_FLUSH_INTERVAL_MS));
        agent_stats_flush();
    }
}

/* ── app_main ───────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s starting", APP_NAME, APP_VERSION);

    /* 1. Default event loop + TCP/IP stack (required before Wi-Fi / WebSocket) */
    ESP_ERROR_CHECK(esp_netif_init());
    esp_event_loop_create_default();

    /* 2. Storage (NVS) — must be first; Wi-Fi requires NVS to be initialized */
    ESP_ERROR_CHECK(hal_storage_init());

    /* 3. Hardware init */
    hal_display_cfg_t disp_cfg = {
        .width          = 320,
        .height         = 240,
        .rotation       = 0,
        .double_buffered = true,
        .buf_size_px    = 320 * 50,
    };
    ESP_ERROR_CHECK(hal_display_create(&disp_cfg, &g_hal.display));
    ESP_ERROR_CHECK(hal_touch_create(&g_hal.touch));
    ESP_ERROR_CHECK(hal_buttons_create(&g_hal.buttons));
    ESP_ERROR_CHECK(hal_imu_create(&g_hal.imu));
    ESP_ERROR_CHECK(hal_led_create(&g_hal.led));

    hal_audio_cfg_t audio_cfg = {
        .sample_rate     = 16000,
        .bits_per_sample = 16,
        .channels        = 1,
        .direction       = HAL_AUDIO_DIR_DUPLEX,
        .buf_size        = 2048,
    };
    ESP_ERROR_CHECK(hal_audio_create(&audio_cfg, &g_hal.audio));

    /* 3b. Audio manager (recording pipeline + playback queue) */
    ESP_ERROR_CHECK(audio_manager_init(g_hal.audio));
    app_notify_init();

    /* 4. UI — show boot screen */
    ui_fonts_init();
    ESP_ERROR_CHECK(ui_manager_init());
    ui_manager_show(UI_SCREEN_BOOT, UI_ANIM_NONE);

    /* Wi-Fi + debug screenshot server start after the display/LVGL buffers
     * (internal RAM) are allocated, since the Wi-Fi driver's RX/TX buffers
     * also compete for internal RAM. */
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(debug_screenshot_start());

    /* 5. State machine */
    ESP_ERROR_CHECK(sm_create(&s_sm));
    sm_register_callback(s_sm, app_sm_callback, NULL);

    /* 6. Protocol adapters */
    proto_t *proto = NULL;

#if CONFIG_PROTO_CLAUDE_BUDDY_ENABLED
    ESP_ERROR_CHECK(proto_claude_buddy_create(&proto));
    ESP_ERROR_CHECK(proto_register(proto));
#elif CONFIG_PROTO_OPENCLAW_ENABLED
    ESP_ERROR_CHECK(proto_openclaw_create(&proto));
    ESP_ERROR_CHECK(proto_register(proto));
#elif CONFIG_PROTO_HERMES_ENABLED
    ESP_ERROR_CHECK(proto_hermes_create(&proto));
    ESP_ERROR_CHECK(proto_register(proto));
#endif

    /* Activate: use Kconfig default (NVS override removed — mode is build-time) */
    if (proto) proto_set_active(proto->name);

    /* 7. Transport layer */
    transport_t *ble = NULL, *ws = NULL;

#if CONFIG_TRANSPORT_BLE_ENABLED
    ESP_ERROR_CHECK(transport_ble_create(&ble, NULL, 0));
    ESP_ERROR_CHECK(transport_register(ble));
#endif

#if CONFIG_TRANSPORT_WS_ENABLED
    ESP_ERROR_CHECK(transport_ws_create(&ws, NULL));
    ESP_ERROR_CHECK(transport_register(ws));
#endif

    /* 8. Agent core */
    ESP_ERROR_CHECK(agent_core_init(s_sm));
    ESP_ERROR_CHECK(agent_core_start());

    /* 9. IMU monitor */
    imu_monitor_start(g_hal.imu);

    /* 9b. Audio manager tasks + PTT button (BTN_0 = center/mute button) */
    ESP_ERROR_CHECK(audio_manager_start());
    ui_button_router_init();
    if (g_hal.buttons) {
        /* BOOT: PTT short-press; long-press opens menu */
        g_hal.buttons->register_cb(g_hal.buttons, HAL_BTN_BOOT,
                                    HAL_BTN_EVT_PRESS_DOWN, ptt_button_cb, NULL);
        g_hal.buttons->register_cb(g_hal.buttons, HAL_BTN_BOOT,
                                    HAL_BTN_EVT_PRESS_UP,   ptt_button_cb, NULL);
        g_hal.buttons->register_cb(g_hal.buttons, HAL_BTN_BOOT,
                                    HAL_BTN_EVT_LONG_PRESS, approve_button_cb, NULL);
        /* LEFT (mute): deny during ATTENTION; back/stats otherwise */
        g_hal.buttons->register_cb(g_hal.buttons, HAL_BTN_LEFT,
                                    HAL_BTN_EVT_PRESS_DOWN, left_button_cb, NULL);
        /* LEFT long-press: open pet stats */
        g_hal.buttons->register_cb(g_hal.buttons, HAL_BTN_LEFT,
                                    HAL_BTN_EVT_LONG_PRESS,
                                    left_long_press_cb, NULL);
    }

    /* 10. Periodic background tasks */
    xTaskCreate(heartbeat_task,    "heartbeat",   2048, NULL, 2, NULL);
    xTaskCreate(stats_persist_task, "stats_flush", 2048, NULL, 1, NULL);

    /* 11. Start transports */
    ESP_ERROR_CHECK(transport_start_all());

    /* 12. Switch to main screen */
    vTaskDelay(pdMS_TO_TICKS(500));
    ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_FADE);

    ESP_LOGI(TAG, "startup complete");
}
