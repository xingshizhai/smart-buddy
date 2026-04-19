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
#include "app_config.h"

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
#define CONFIG_TRANSPORT_WS_ENABLED 1
#endif
#ifndef CONFIG_TRANSPORT_USB_ENABLED
#define CONFIG_TRANSPORT_USB_ENABLED 1
#endif

static sm_handle_t s_sm = NULL;

/* ── Push-to-talk ────────────────────────────────────────────────────── */

static void ptt_button_cb(hal_button_id_t id, hal_button_event_t evt, void *ctx)
{
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

/* ── Periodic tasks ─────────────────────────────────────────────────── */

static void heartbeat_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
        proto_t *proto = proto_get_active();
        if (!proto) continue;

        proto_out_msg_t msg = {.type = PROTO_MSG_HEARTBEAT_ACK};
        uint8_t *enc = NULL; size_t enc_len = 0;
        if (proto->encode(proto, &msg, &enc, &enc_len) == ESP_OK) {
            transport_send_all(enc, enc_len);
            free(enc);
        }
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

    /* 2. Storage (NVS) — must be first */
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

    /* 4. UI — show boot screen */
    ESP_ERROR_CHECK(ui_manager_init());
    ui_manager_show(UI_SCREEN_BOOT, UI_ANIM_NONE);

    /* 5. State machine */
    ESP_ERROR_CHECK(sm_create(&s_sm));
    sm_register_callback(s_sm, ui_manager_on_state_change, NULL);

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
    transport_t *ble = NULL, *ws = NULL, *usb = NULL;

#if CONFIG_TRANSPORT_BLE_ENABLED
    ESP_ERROR_CHECK(transport_ble_create(&ble, NULL, 0));
    ESP_ERROR_CHECK(transport_register(ble));
#endif

#if CONFIG_TRANSPORT_WS_ENABLED
    ESP_ERROR_CHECK(transport_ws_create(&ws, NULL));
    ESP_ERROR_CHECK(transport_register(ws));
#endif

#if CONFIG_TRANSPORT_USB_ENABLED
    ESP_ERROR_CHECK(transport_usb_create(&usb));
    ESP_ERROR_CHECK(transport_register(usb));
#endif

    /* 8. Agent core */
    ESP_ERROR_CHECK(agent_core_init(s_sm));
    ESP_ERROR_CHECK(agent_core_start());

    /* 9. IMU monitor */
    imu_monitor_start(g_hal.imu);

    /* 9b. Audio manager tasks + PTT button (BTN_0 = center/mute button) */
    ESP_ERROR_CHECK(audio_manager_start());
    if (g_hal.buttons) {
        g_hal.buttons->register_cb(g_hal.buttons, HAL_BTN_BOOT,
                                    HAL_BTN_EVT_PRESS_DOWN, ptt_button_cb, NULL);
        g_hal.buttons->register_cb(g_hal.buttons, HAL_BTN_BOOT,
                                    HAL_BTN_EVT_PRESS_UP,   ptt_button_cb, NULL);
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
