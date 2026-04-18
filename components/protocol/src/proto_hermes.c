#include "sdkconfig.h"
#if CONFIG_PROTO_HERMES_ENABLED

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "protocol/protocol.h"

#define TAG "HERMES"

typedef enum {
    HERMES_FRAMING_NEWLINE = 0,
    HERMES_FRAMING_LENGTH_PREFIX,
} hermes_framing_t;

typedef struct {
    proto_t          base;
    hermes_framing_t framing;
    char field_session_count[32];
    char field_approval_id[32];
    char field_approval_tool[32];
    char field_token_total[32];
    char msg_type_approval[32];
    char msg_type_session[32];
    uint32_t token_milestone;
    uint32_t last_token_total;
} hermes_ctx_t;

static esp_err_t hermes_decode(proto_t *proto,
                                const uint8_t *data, size_t len,
                                agent_event_t *out_events,
                                size_t max_events,
                                size_t *n_events)
{
    hermes_ctx_t *ctx = (hermes_ctx_t *)proto;
    *n_events = 0;

    const char *payload = (const char *)data;
    size_t payload_len  = len;

    if (ctx->framing == HERMES_FRAMING_LENGTH_PREFIX) {
        if (len < 4) return ESP_OK;
        uint32_t msg_len = ((uint32_t)data[0])       |
                           ((uint32_t)data[1] << 8)  |
                           ((uint32_t)data[2] << 16) |
                           ((uint32_t)data[3] << 24);
        if (len < 4 + msg_len) return ESP_OK;
        payload     = (const char *)(data + 4);
        payload_len = msg_len;
    }

    char *str = strndup(payload, payload_len);
    if (!str) return ESP_ERR_NO_MEM;

    cJSON *root = cJSON_Parse(str);
    free(str);
    if (!root) return ESP_OK;

    size_t idx = 0;

    /* Session update by configurable field name */
    cJSON *sess = cJSON_GetObjectItem(root, ctx->field_session_count);
    cJSON *tok  = cJSON_GetObjectItem(root, ctx->field_token_total);
    if (sess && idx < max_events) {
        agent_event_t *e = &out_events[idx++];
        memset(e, 0, sizeof(*e));
        e->type = AGENT_EVT_SESSION_UPDATE;
        e->data.session.running      = (uint32_t)sess->valuedouble;
        e->data.session.tokens_total = tok ? (uint32_t)tok->valuedouble : 0;
        e->timestamp_us = esp_timer_get_time();

        uint32_t t = e->data.session.tokens_total;
        if (t >= ctx->token_milestone && ctx->last_token_total < ctx->token_milestone
            && idx < max_events) {
            agent_event_t *me = &out_events[idx++];
            memset(me, 0, sizeof(*me));
            me->type = AGENT_EVT_TOKEN_UPDATE;
            me->data.tokens.total = t;
            me->timestamp_us = esp_timer_get_time();
            ctx->token_milestone += CONFIG_PROTO_TOKEN_MILESTONE;
        }
        ctx->last_token_total = t;
    }

    /* Approval request by configurable field names */
    cJSON *appr_id   = cJSON_GetObjectItem(root, ctx->field_approval_id);
    cJSON *appr_tool = cJSON_GetObjectItem(root, ctx->field_approval_tool);
    if (appr_id && idx < max_events) {
        agent_event_t *e = &out_events[idx++];
        memset(e, 0, sizeof(*e));
        e->type = AGENT_EVT_APPROVAL_REQUEST;
        if (appr_id->valuestring)
            strlcpy(e->data.approval_req.id, appr_id->valuestring,
                    sizeof(e->data.approval_req.id));
        if (appr_tool && appr_tool->valuestring)
            strlcpy(e->data.approval_req.tool, appr_tool->valuestring,
                    sizeof(e->data.approval_req.tool));
        e->timestamp_us = esp_timer_get_time();
    }

    *n_events = idx;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t hermes_encode(proto_t *proto,
                                const proto_out_msg_t *msg,
                                uint8_t **out_data, size_t *out_len)
{
    /* Re-use OpenClaw-compatible JSON for now; update when Hermes spec is known */
    hermes_ctx_t *ctx = (hermes_ctx_t *)proto;
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    switch (msg->type) {
    case PROTO_MSG_APPROVAL_RESPONSE:
        cJSON_AddStringToObject(root, ctx->field_approval_id, msg->approval.id);
        cJSON_AddBoolToObject(root, "approved", msg->approval.approved);
        break;
    default:
        cJSON_AddStringToObject(root, "cmd", "ack");
        break;
    }

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    size_t slen = strlen(str);

    if (ctx->framing == HERMES_FRAMING_LENGTH_PREFIX) {
        *out_data = malloc(4 + slen);
        if (!*out_data) { free(str); return ESP_ERR_NO_MEM; }
        (*out_data)[0] = slen & 0xFF;
        (*out_data)[1] = (slen >> 8) & 0xFF;
        (*out_data)[2] = (slen >> 16) & 0xFF;
        (*out_data)[3] = (slen >> 24) & 0xFF;
        memcpy(*out_data + 4, str, slen);
        *out_len = 4 + slen;
    } else {
        *out_data = malloc(slen + 2);
        if (!*out_data) { free(str); return ESP_ERR_NO_MEM; }
        memcpy(*out_data, str, slen);
        (*out_data)[slen]     = '\n';
        (*out_data)[slen + 1] = '\0';
        *out_len = slen + 1;
    }
    free(str);
    return ESP_OK;
}

static esp_err_t hermes_init(proto_t *proto, const void *cfg) { return ESP_OK; }
static void      hermes_deinit(proto_t *proto) { }

esp_err_t proto_hermes_create(proto_t **out)
{
    hermes_ctx_t *ctx = calloc(1, sizeof(hermes_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    /* Default field names match OpenClaw; update when Hermes spec is available */
    strlcpy(ctx->field_session_count, "running",   sizeof(ctx->field_session_count));
    strlcpy(ctx->field_approval_id,   "id",        sizeof(ctx->field_approval_id));
    strlcpy(ctx->field_approval_tool, "tool",      sizeof(ctx->field_approval_tool));
    strlcpy(ctx->field_token_total,   "tokens",    sizeof(ctx->field_token_total));
    strlcpy(ctx->msg_type_approval,   "permission_request", sizeof(ctx->msg_type_approval));
    strlcpy(ctx->msg_type_session,    "session_update",     sizeof(ctx->msg_type_session));
    ctx->token_milestone   = CONFIG_PROTO_TOKEN_MILESTONE;
    ctx->last_token_total  = 0;
    ctx->framing           = HERMES_FRAMING_NEWLINE;

    ctx->base.name   = "hermes";
    ctx->base.decode = hermes_decode;
    ctx->base.encode = hermes_encode;
    ctx->base.init   = hermes_init;
    ctx->base.deinit = hermes_deinit;
    ctx->base.priv   = ctx;

    *out = (proto_t *)ctx;
    return ESP_OK;
}

#endif /* CONFIG_PROTO_HERMES_ENABLED */
