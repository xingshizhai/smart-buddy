#include "sdkconfig.h"
#if CONFIG_PROTO_OPENCLAW_ENABLED

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "protocol/protocol.h"

#define TAG "OPENCLAW"

typedef struct {
    proto_t  base;
    uint32_t token_milestone;
    char     owner_name[32];
    uint32_t last_token_total;
} openclaw_ctx_t;

static esp_err_t openclaw_decode(proto_t *proto,
                                  const uint8_t *data, size_t len,
                                  agent_event_t *out_events,
                                  size_t max_events,
                                  size_t *n_events)
{
    openclaw_ctx_t *ctx = (openclaw_ctx_t *)proto;
    *n_events = 0;

    /* Skip trailing newline for parse */
    char *str = strndup((const char *)data, len);
    if (!str) return ESP_ERR_NO_MEM;

    cJSON *root = cJSON_Parse(str);
    free(str);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return ESP_OK;
    }

    size_t idx = 0;

    /* Session/heartbeat snapshot */
    cJSON *total   = cJSON_GetObjectItem(root, "total");
    cJSON *running = cJSON_GetObjectItem(root, "running");
    cJSON *tokens  = cJSON_GetObjectItem(root, "tokens");

    if (total && idx < max_events) {
        agent_event_t *e = &out_events[idx++];
        memset(e, 0, sizeof(*e));
        e->type = AGENT_EVT_SESSION_UPDATE;
        e->data.session.running      = running ? (uint32_t)running->valuedouble : 0;
        e->data.session.tokens_total = tokens  ? (uint32_t)tokens->valuedouble  : 0;
        e->timestamp_us = esp_timer_get_time();

        /* Token milestone check */
        uint32_t tok = e->data.session.tokens_total;
        if (tok >= ctx->token_milestone &&
            ctx->last_token_total < ctx->token_milestone &&
            idx < max_events) {
            agent_event_t *me = &out_events[idx++];
            memset(me, 0, sizeof(*me));
            me->type = AGENT_EVT_TOKEN_UPDATE;
            me->data.tokens.total = tok;
            me->timestamp_us = esp_timer_get_time();
            ctx->token_milestone += CONFIG_PROTO_TOKEN_MILESTONE;
        }
        ctx->last_token_total = tok;
    }

    /* Approval prompt */
    cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
    if (prompt && idx < max_events) {
        cJSON *id   = cJSON_GetObjectItem(prompt, "id");
        cJSON *tool = cJSON_GetObjectItem(prompt, "tool");
        cJSON *hint = cJSON_GetObjectItem(prompt, "hint");

        agent_event_t *e = &out_events[idx++];
        memset(e, 0, sizeof(*e));
        e->type = AGENT_EVT_APPROVAL_REQUEST;
        if (id   && id->valuestring)   strlcpy(e->data.approval_req.id,   id->valuestring,   sizeof(e->data.approval_req.id));
        if (tool && tool->valuestring) strlcpy(e->data.approval_req.tool, tool->valuestring, sizeof(e->data.approval_req.tool));
        if (hint && hint->valuestring) strlcpy(e->data.approval_req.hint, hint->valuestring, sizeof(e->data.approval_req.hint));
        e->timestamp_us = esp_timer_get_time();
    }

    *n_events = idx;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t openclaw_encode(proto_t *proto,
                                  const proto_out_msg_t *msg,
                                  uint8_t **out_data, size_t *out_len)
{
    openclaw_ctx_t *ctx = (openclaw_ctx_t *)proto;
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    switch (msg->type) {
    case PROTO_MSG_APPROVAL_RESPONSE:
        cJSON_AddStringToObject(root, "cmd", "permission");
        cJSON_AddStringToObject(root, "id", msg->approval.id);
        cJSON_AddStringToObject(root, "decision",
                                 msg->approval.approved ? "once" : "deny");
        break;

    case PROTO_MSG_TIME_SYNC: {
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg->time_sync.epoch_s));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg->time_sync.tz_offset_s));
        cJSON_AddItemToObject(root, "time", arr);
        break;
    }

    case PROTO_MSG_DEVICE_NAME:
        cJSON_AddStringToObject(root, "cmd", "owner");
        cJSON_AddStringToObject(root, "name", msg->device.name);
        break;

    case PROTO_MSG_STATUS_REQUEST:
        cJSON_AddStringToObject(root, "cmd", "status");
        break;

    case PROTO_MSG_HEARTBEAT_ACK:
        cJSON_AddStringToObject(root, "cmd", "ack");
        break;
    }

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    size_t slen = strlen(str);
    *out_data = malloc(slen + 2);
    if (!*out_data) { free(str); return ESP_ERR_NO_MEM; }
    memcpy(*out_data, str, slen);
    (*out_data)[slen]     = '\n';
    (*out_data)[slen + 1] = '\0';
    *out_len = slen + 1;
    free(str);
    return ESP_OK;
}

static esp_err_t openclaw_init(proto_t *proto, const void *cfg)
{
    openclaw_ctx_t *ctx = (openclaw_ctx_t *)proto;
    ctx->token_milestone = CONFIG_PROTO_TOKEN_MILESTONE;
    ctx->last_token_total = 0;
    return ESP_OK;
}

static void openclaw_deinit(proto_t *proto) { }

esp_err_t proto_openclaw_create(proto_t **out)
{
    openclaw_ctx_t *ctx = calloc(1, sizeof(openclaw_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    ctx->base.name   = "openclaw";
    ctx->base.decode = openclaw_decode;
    ctx->base.encode = openclaw_encode;
    ctx->base.init   = openclaw_init;
    ctx->base.deinit = openclaw_deinit;
    ctx->base.priv   = ctx;

    openclaw_init((proto_t *)ctx, NULL);
    *out = (proto_t *)ctx;
    return ESP_OK;
}

#endif /* CONFIG_PROTO_OPENCLAW_ENABLED */
