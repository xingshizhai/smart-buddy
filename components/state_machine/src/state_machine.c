#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "state_machine.h"

#define TAG "SM"

#ifndef CONFIG_UI_HEART_DURATION_MS
#define CONFIG_UI_HEART_DURATION_MS     2000
#endif
#ifndef CONFIG_UI_CELEBRATE_DURATION_MS
#define CONFIG_UI_CELEBRATE_DURATION_MS 3000
#endif
#ifndef CONFIG_UI_DIZZY_DURATION_MS
#define CONFIG_UI_DIZZY_DURATION_MS     2000
#endif

typedef struct sm_ctx_s {
    sm_state_t      state;
    sm_state_t      prev_state;
    sm_state_cb_t   cb;
    void           *cb_ctx;
    SemaphoreHandle_t lock;
    esp_timer_handle_t timer;
    int64_t         attention_enter_us;
} sm_ctx_t;

static void timer_cb(void *arg)
{
    sm_handle_t h = (sm_handle_t)arg;
    sm_event_t evt = {.type = SM_EVT_TIMER_EXPIRED, .timestamp_us = esp_timer_get_time()};
    sm_post_event(h, &evt);
}

esp_err_t sm_create(sm_handle_t *out)
{
    sm_ctx_t *ctx = calloc(1, sizeof(sm_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    ctx->state      = SM_STATE_SLEEP;
    ctx->prev_state = SM_STATE_SLEEP;
    ctx->lock       = xSemaphoreCreateMutex();

    esp_timer_create_args_t targs = {
        .callback  = timer_cb,
        .arg       = ctx,
        .name      = "sm_timer",
    };
    esp_timer_create(&targs, &ctx->timer);

    *out = ctx;
    return ESP_OK;
}

void sm_destroy(sm_handle_t handle)
{
    if (!handle) return;
    sm_ctx_t *ctx = (sm_ctx_t *)handle;
    esp_timer_delete(ctx->timer);
    vSemaphoreDelete(ctx->lock);
    free(ctx);
}

static void enter_state(sm_ctx_t *ctx, sm_state_t new_state, uint32_t timer_ms)
{
    sm_state_t old = ctx->state;
    if (old != SM_STATE_SLEEP      &&
        old != SM_STATE_ATTENTION  &&
        new_state == SM_STATE_SLEEP) {
        ctx->prev_state = old;
    }
    if (new_state == SM_STATE_ATTENTION ||
        new_state == SM_STATE_CELEBRATE ||
        new_state == SM_STATE_DIZZY     ||
        new_state == SM_STATE_HEART) {
        /* Don't overwrite prev_state when HEART follows ATTENTION —
         * keep the pre-ATTENTION state so the timer return is correct. */
        bool skip = (new_state == SM_STATE_HEART && old == SM_STATE_ATTENTION);
        if (!skip && old != SM_STATE_SLEEP && old != new_state)
            ctx->prev_state = old;
    }

    esp_timer_stop(ctx->timer);
    ctx->state = new_state;

    if (timer_ms > 0)
        esp_timer_start_once(ctx->timer, (uint64_t)timer_ms * 1000);

    if (ctx->cb)
        ctx->cb(new_state, old, ctx->cb_ctx);

    ESP_LOGI(TAG, "%s → %s", sm_state_name(old), sm_state_name(new_state));
}

esp_err_t sm_post_event(sm_handle_t handle, const sm_event_t *evt)
{
    sm_ctx_t *ctx = (sm_ctx_t *)handle;
    if (!ctx || !evt) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    sm_state_t s = ctx->state;

    switch (evt->type) {
    case SM_EVT_TRANSPORT_DISCONNECTED:
        enter_state(ctx, SM_STATE_SLEEP, 0);
        break;

    case SM_EVT_FACE_DOWN:
        if (s != SM_STATE_ATTENTION)
            enter_state(ctx, SM_STATE_SLEEP, 0);
        break;

    case SM_EVT_TRANSPORT_CONNECTED:
    case SM_EVT_FACE_UP:
        if (s == SM_STATE_SLEEP)
            enter_state(ctx, SM_STATE_IDLE, 0);
        break;

    case SM_EVT_SESSION_STARTED:
        /* Also exit ATTENTION when a new session fires — prompt vanished externally */
        if (s == SM_STATE_IDLE || s == SM_STATE_ATTENTION)
            enter_state(ctx, SM_STATE_BUSY, 0);
        break;

    case SM_EVT_SESSION_ENDED:
        /* Also exit ATTENTION when session ends — prompt resolved or timed out */
        if (s == SM_STATE_BUSY || s == SM_STATE_ATTENTION)
            enter_state(ctx, SM_STATE_IDLE, 0);
        break;

    case SM_EVT_APPROVAL_REQUEST:
        if (s != SM_STATE_ATTENTION && s != SM_STATE_SLEEP) {
            ctx->attention_enter_us = esp_timer_get_time();
            enter_state(ctx, SM_STATE_ATTENTION, 0);
        }
        break;

    case SM_EVT_APPROVAL_RESOLVED: {
        if (s == SM_STATE_ATTENTION) {
            int64_t elapsed_ms = (esp_timer_get_time() - ctx->attention_enter_us) / 1000;
            if (evt->data.approved && elapsed_ms < 5000)
                enter_state(ctx, SM_STATE_HEART, CONFIG_UI_HEART_DURATION_MS);
            else
                enter_state(ctx, ctx->prev_state, 0);
        }
        break;
    }

    case SM_EVT_TOKEN_MILESTONE:
        if (s != SM_STATE_ATTENTION && s != SM_STATE_SLEEP)
            enter_state(ctx, SM_STATE_CELEBRATE, CONFIG_UI_CELEBRATE_DURATION_MS);
        break;

    case SM_EVT_SHAKE_DETECTED:
        if (s != SM_STATE_ATTENTION && s != SM_STATE_SLEEP)
            enter_state(ctx, SM_STATE_DIZZY, CONFIG_UI_DIZZY_DURATION_MS);
        break;

    case SM_EVT_TIMER_EXPIRED:
        if (s == SM_STATE_CELEBRATE || s == SM_STATE_DIZZY || s == SM_STATE_HEART)
            enter_state(ctx, ctx->prev_state, 0);
        break;

    default:
        break;
    }

    xSemaphoreGive(ctx->lock);
    return ESP_OK;
}

sm_state_t sm_get_state(sm_handle_t handle)
{
    sm_ctx_t *ctx = (sm_ctx_t *)handle;
    return ctx ? ctx->state : SM_STATE_SLEEP;
}

sm_state_t sm_get_prev_state(sm_handle_t handle)
{
    sm_ctx_t *ctx = (sm_ctx_t *)handle;
    return ctx ? ctx->prev_state : SM_STATE_SLEEP;
}

esp_err_t sm_register_callback(sm_handle_t handle, sm_state_cb_t cb, void *ctx)
{
    sm_ctx_t *h = (sm_ctx_t *)handle;
    if (!h) return ESP_ERR_INVALID_ARG;
    h->cb     = cb;
    h->cb_ctx = ctx;
    return ESP_OK;
}

const char *sm_state_name(sm_state_t state)
{
    static const char *names[] = {
        "SLEEP", "IDLE", "BUSY", "ATTENTION",
        "CELEBRATE", "DIZZY", "HEART",
    };
    if (state < SM_STATE_MAX) return names[state];
    return "UNKNOWN";
}
