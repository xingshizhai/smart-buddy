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
#include "protocol/protocol.h"

#define TAG          "AGENT"
#define QUEUE_DEPTH  32
#define TASK_STACK   8192
#define TASK_PRIO    6

static QueueHandle_t s_queue = NULL;
static sm_handle_t   s_sm    = NULL;
static TaskHandle_t  s_task  = NULL;

/* Pending approval ID for response encoding */
static char s_pending_id[64] = {0};

static void on_transport_rx(transport_id_t id, const uint8_t *data, size_t len, void *ctx)
{
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
            sm_evt.type = connected ? SM_EVT_TRANSPORT_CONNECTED
                                    : SM_EVT_TRANSPORT_DISCONNECTED;
            sm_post_event(s_sm, &sm_evt);
            if (connected) send_time_sync();
            break;
        }

        case AGENT_EVT_SESSION_UPDATE: {
            uint32_t running = evt.data.session.running;
            if (running > 0) {
                sm_evt.type = SM_EVT_SESSION_STARTED;
                sm_post_event(s_sm, &sm_evt);
            } else {
                sm_evt.type = SM_EVT_SESSION_ENDED;
                sm_post_event(s_sm, &sm_evt);
            }
            agent_stats_update_tokens(evt.data.session.tokens_total,
                                       evt.data.session.tokens_today);
            break;
        }

        case AGENT_EVT_TOKEN_UPDATE:
            sm_evt.type = SM_EVT_TOKEN_MILESTONE;
            sm_evt.data.token_count = evt.data.tokens.total;
            sm_post_event(s_sm, &sm_evt);
            break;

        case AGENT_EVT_APPROVAL_REQUEST:
            strlcpy(s_pending_id, evt.data.approval_req.id, sizeof(s_pending_id));
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
