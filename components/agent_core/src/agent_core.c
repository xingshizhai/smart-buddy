#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "agent_core.h"
#include "agent_stats.h"
#include "state_machine.h"
#include "transport/transport.h"
#include <stdbool.h>
#include "protocol/protocol.h"
#include "ui/ui_manager.h"

#define TAG          "AGENT"
#define QUEUE_DEPTH  32
#define TASK_STACK   8192
#define TASK_PRIO    6

static QueueHandle_t s_queue = NULL;
static sm_handle_t   s_sm    = NULL;
static TaskHandle_t  s_task  = NULL;

/* Pending approval ID for response encoding */
static char s_pending_id[64] = {0};

/* Per-transport connected state — only go SLEEP when all actually disconnect */
static bool s_transport_up[TRANSPORT_MAX_INSTANCES] = {false};
static int  s_connected_count = 0;

/* Turn-complete: briefly flash BUSY for 1.5 s then auto-return to IDLE */
static esp_timer_handle_t s_turn_timer = NULL;

static void turn_timer_cb(void *arg)
{
    sm_event_t e = {.type = SM_EVT_SESSION_ENDED, .timestamp_us = esp_timer_get_time()};
    sm_post_event(s_sm, &e);
}

static void on_transport_rx(transport_id_t id, const uint8_t *data, size_t len, void *ctx)
{
    ESP_LOGI(TAG, "RX [t=%d]: %.*s", id, (int)len, (const char *)data);

    proto_t *proto = proto_get_active();
    if (!proto) return;

    agent_event_t events[8];
    size_t n = 0;
    proto->decode(proto, data, len, events, 8, &n);
    for (size_t i = 0; i < n; i++)
        agent_core_post_event(&events[i]);
}

static void on_transport_state(transport_id_t id, transport_state_t state, void *ctx)
{
    agent_event_t evt = {
        .type = AGENT_EVT_TRANSPORT_STATE,
        .data.transport = {.transport_id = id, .connected = (state == TRANSPORT_STATE_CONNECTED)},
        .timestamp_us = esp_timer_get_time(),
    };
    agent_core_post_event(&evt);
}

static void send_approval_response(const char *id, bool approved)
{
    proto_t *proto = proto_get_active();
    if (!proto) return;

    proto_out_msg_t msg = {
        .type = PROTO_MSG_APPROVAL_RESPONSE,
        .approval = {.approved = approved},
    };
    strlcpy(msg.approval.id, id, sizeof(msg.approval.id));

    uint8_t *encoded = NULL;
    size_t   enc_len = 0;
    if (proto->encode(proto, &msg, &encoded, &enc_len) == ESP_OK) {
        transport_send_all(encoded, enc_len);
        free(encoded);
    }
    agent_stats_record_approval(approved);
}


static void send_time_sync(void)
{
    proto_t *proto = proto_get_active();
    if (!proto) return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    proto_out_msg_t msg = {
        .type = PROTO_MSG_TIME_SYNC,
        .time_sync = {.epoch_s = tv.tv_sec, .tz_offset_s = 0},
    };
    uint8_t *enc = NULL; size_t enc_len = 0;
    if (proto->encode(proto, &msg, &enc, &enc_len) == ESP_OK) {
        transport_send_all(enc, enc_len);
        free(enc);
    }
}

static void agent_task(void *arg)
{
    agent_event_t evt;
    for (;;) {
        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(5000)) != pdTRUE) {
            continue;
        }

        sm_event_t sm_evt = {.timestamp_us = evt.timestamp_us};

        switch (evt.type) {
        case AGENT_EVT_TRANSPORT_STATE: {
            bool connected = evt.data.transport.connected;
            uint8_t tid = evt.data.transport.transport_id;
            if (tid >= TRANSPORT_MAX_INSTANCES) break;

            if (connected && !s_transport_up[tid]) {
                s_transport_up[tid] = true;
                s_connected_count++;
                sm_evt.type = SM_EVT_TRANSPORT_CONNECTED;
                sm_post_event(s_sm, &sm_evt);
                send_time_sync();
            } else if (connected && s_transport_up[tid] && tid == TRANSPORT_ID_BLE) {
                /* Re-fired after CCCD subscription — desktop will send owner/name cmds */
                ESP_LOGI(TAG, "BLE subscribed");
            } else if (!connected && s_transport_up[tid]) {
                s_transport_up[tid] = false;
                s_connected_count--;
                if (s_connected_count == 0) {
                    sm_evt.type = SM_EVT_TRANSPORT_DISCONNECTED;
                    sm_post_event(s_sm, &sm_evt);
                }
            }
            if (tid == TRANSPORT_ID_BLE)
                ui_screen_main_set_ble_connected(connected);
            break;
        }

        case AGENT_EVT_SESSION_UPDATE: {
            uint32_t running = evt.data.session.running;
            uint32_t waiting = evt.data.session.waiting;
            /* Drive state machine: waiting sessions take priority over running */
            if (waiting == 0 && running > 0) {
                sm_evt.type = SM_EVT_SESSION_STARTED;
                sm_post_event(s_sm, &sm_evt);
                agent_stats_record_session_start();
            } else if (waiting == 0 && running == 0) {
                sm_evt.type = SM_EVT_SESSION_ENDED;
                sm_post_event(s_sm, &sm_evt);
            }
            /* Update stats + display */
            agent_stats_update_tokens(evt.data.session.tokens_total,
                                       evt.data.session.tokens_today);
            ui_screen_main_set_token_count(evt.data.session.tokens_total);
            ui_screen_main_set_msg(evt.data.session.msg);
            ui_screen_main_set_entries(
                (const char (*)[92])evt.data.session.entries,
                evt.data.session.n_entries);
            break;
        }

        case AGENT_EVT_TOKEN_UPDATE:
            sm_evt.type = SM_EVT_TOKEN_MILESTONE;
            sm_evt.data.token_count = evt.data.tokens.total;
            sm_post_event(s_sm, &sm_evt);
            break;

        case AGENT_EVT_APPROVAL_REQUEST:
            strlcpy(s_pending_id, evt.data.approval_req.id, sizeof(s_pending_id));
            /* Populate approval screen before switching to it */
            ui_screen_approval_set_prompt(evt.data.approval_req.tool,
                                          evt.data.approval_req.hint,
                                          evt.data.approval_req.id);
            sm_evt.type = SM_EVT_APPROVAL_REQUEST;
            sm_post_event(s_sm, &sm_evt);
            break;

        case AGENT_EVT_APPROVAL_RESOLVED:
            send_approval_response(evt.data.approval_resp.id,
                                   evt.data.approval_resp.approved);
            sm_evt.type = SM_EVT_APPROVAL_RESOLVED;
            sm_evt.data.approved = evt.data.approval_resp.approved;
            sm_post_event(s_sm, &sm_evt);
            break;

        case AGENT_EVT_IMU_GESTURE:
            switch (evt.data.imu.gesture) {
            case IMU_GESTURE_SHAKE:
                sm_evt.type = SM_EVT_SHAKE_DETECTED; break;
            case IMU_GESTURE_FACE_DOWN:
                sm_evt.type = SM_EVT_FACE_DOWN; break;
            case IMU_GESTURE_FACE_UP:
                sm_evt.type = SM_EVT_FACE_UP; break;
            }
            sm_post_event(s_sm, &sm_evt);
            break;

        case AGENT_EVT_TURN_COMPLETE:
            /* Flash BUSY for 1.5 s so the display shows activity */
            sm_evt.type = SM_EVT_SESSION_STARTED;
            sm_post_event(s_sm, &sm_evt);
            if (s_turn_timer) {
                esp_timer_stop(s_turn_timer);
                esp_timer_start_once(s_turn_timer, 1500 * 1000);
            }
            break;

        case AGENT_EVT_CMD: {
            /* Incoming desktop command — send required ack per protocol spec.
             * Without acks the desktop may withhold heartbeats. */
            const char *cmd = evt.data.cmd.name;
            ESP_LOGI(TAG, "cmd: %s value=%s", cmd, evt.data.cmd.value);

            proto_t *proto = proto_get_active();
            if (!proto) break;

            proto_out_msg_t ack = {
                .type = PROTO_MSG_CMD_ACK,
                .cmd_ack = {.ok = true},
            };
            strlcpy(ack.cmd_ack.cmd, cmd, sizeof(ack.cmd_ack.cmd));

            uint8_t *enc = NULL; size_t enc_len = 0;
            if (proto->encode(proto, &ack, &enc, &enc_len) == ESP_OK) {
                transport_send_all(enc, enc_len);
                free(enc);
            }
            break;
        }

        default:
            break;
        }
    }
}

esp_err_t agent_core_init(sm_handle_t sm)
{
    s_sm    = sm;
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(agent_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    transport_set_callbacks(on_transport_rx, on_transport_state, NULL);

    esp_timer_create_args_t ta = {
        .callback = turn_timer_cb,
        .name     = "turn_end",
    };
    esp_timer_create(&ta, &s_turn_timer);

    return agent_stats_init();
}

esp_err_t agent_core_start(void)
{
    BaseType_t r = xTaskCreatePinnedToCore(agent_task, "agent_core",
                                            TASK_STACK, NULL, TASK_PRIO,
                                            &s_task, 0);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t agent_core_stop(void)
{
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    return ESP_OK;
}

esp_err_t agent_core_post_event(const agent_event_t *evt)
{
    if (!s_queue) return ESP_ERR_INVALID_STATE;
    xQueueSend(s_queue, evt, pdMS_TO_TICKS(10));
    return ESP_OK;
}
