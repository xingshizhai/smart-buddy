#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    SM_STATE_SLEEP     = 0,
    SM_STATE_IDLE,
    SM_STATE_BUSY,
    SM_STATE_ATTENTION,
    SM_STATE_CELEBRATE,
    SM_STATE_DIZZY,
    SM_STATE_HEART,
    SM_STATE_MAX,
} sm_state_t;

typedef enum {
    SM_EVT_TRANSPORT_CONNECTED    = 0,
    SM_EVT_TRANSPORT_DISCONNECTED,
    SM_EVT_SESSION_STARTED,
    SM_EVT_SESSION_ENDED,
    SM_EVT_APPROVAL_REQUEST,
    SM_EVT_APPROVAL_RESOLVED,
    SM_EVT_TOKEN_MILESTONE,
    SM_EVT_SHAKE_DETECTED,
    SM_EVT_FACE_DOWN,
    SM_EVT_FACE_UP,
    SM_EVT_TIMER_EXPIRED,
    SM_EVT_MAX,
} sm_event_type_t;

typedef struct {
    sm_event_type_t type;
    union {
        bool     approved;
        uint32_t token_count;
        uint8_t  transport_id;
    } data;
    int64_t timestamp_us;
} sm_event_t;

typedef void (*sm_state_cb_t)(sm_state_t new_state, sm_state_t old_state, void *ctx);

typedef struct sm_ctx_s *sm_handle_t;

esp_err_t  sm_create(sm_handle_t *out);
void       sm_destroy(sm_handle_t handle);
esp_err_t  sm_post_event(sm_handle_t handle, const sm_event_t *evt);
sm_state_t sm_get_state(sm_handle_t handle);
sm_state_t sm_get_prev_state(sm_handle_t handle);
esp_err_t  sm_register_callback(sm_handle_t handle, sm_state_cb_t cb, void *ctx);

const char *sm_state_name(sm_state_t state);
