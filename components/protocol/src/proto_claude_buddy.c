#include "sdkconfig.h"
#if CONFIG_PROTO_CLAUDE_BUDDY_ENABLED

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"
#include "protocol/protocol.h"
#include "transport/transport.h"

#define TAG "CLAUDE_BUDDY"

#ifndef CONFIG_PROTO_TOKEN_MILESTONE
#define CONFIG_PROTO_TOKEN_MILESTONE 50000
#endif

typedef struct {
    proto_t  base;
    uint32_t token_milestone;
    uint32_t last_token_total;
    char     last_prompt_id[64]; /* dedup: only fire APPROVAL_REQUEST on new id */
} claude_buddy_ctx_t;

/*
 * Wire protocol — claude-desktop-buddy compatible
 *
 * Inbound heartbeat (desktop → device), one JSON object per line:
 *   {
 *     "total":        <uint>,        // cumulative session count
 *     "running":      <uint>,        // sessions currently executing
 *     "waiting":      <uint>,        // sessions awaiting approval
 *     "tokens":       <uint>,        // lifetime token count
 *     "tokens_today": <uint>,        // today's token count
 *     "msg":          <string>,      // optional status message
 *     "entries":      [<string>...], // optional transcript lines
 *     "prompt": {                    // only present when waiting > 0
 *       "id":   <string>,
 *       "tool": <string>,
 *       "hint": <string>
 *     }
 *   }
 *
 * Outbound approval response (device → desktop):
 *   {"cmd":"permission","id":"<id>","decision":"once"|"deny"}\n
 *
 * Outbound heartbeat ack (device → desktop):
 *   {"type":"ack"}\n
 */

static esp_err_t claude_buddy_decode(proto_t *proto,
                                      const uint8_t *data, size_t len,
                                      agent_event_t *out_events,
                                      size_t max_events,
                                      size_t *n_events)
{
    claude_buddy_ctx_t *ctx = (claude_buddy_ctx_t *)proto;
    *n_events = 0;

    char *str = strndup((const char *)data, len);
    if (!str) return ESP_ERR_NO_MEM;

    cJSON *root = cJSON_Parse(str);
    free(str);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return ESP_OK;
    }

    size_t idx = 0;

    cJSON *running_j      = cJSON_GetObjectItem(root, "running");
    cJSON *waiting_j      = cJSON_GetObjectItem(root, "waiting");
    cJSON *tokens_j       = cJSON_GetObjectItem(root, "tokens");
    cJSON *tokens_today_j = cJSON_GetObjectItem(root, "tokens_today");
    cJSON *total_j        = cJSON_GetObjectItem(root, "total");

    /* Heartbeat: has any of running / waiting / tokens / total */
    if ((running_j || waiting_j || tokens_j || tokens_today_j || total_j)
        && idx < max_events) {

        agent_event_t *e = &out_events[idx++];
        memset(e, 0, sizeof(*e));
        e->type = AGENT_EVT_SESSION_UPDATE;
        e->data.session.running      = running_j      ? (uint32_t)running_j->valuedouble      : 0;
        e->data.session.waiting      = waiting_j      ? (uint32_t)waiting_j->valuedouble      : 0;
        e->data.session.tokens_total = tokens_j       ? (uint32_t)tokens_j->valuedouble       : 0;
        e->data.session.tokens_today = tokens_today_j ? (uint32_t)tokens_today_j->valuedouble : 0;
        e->timestamp_us = esp_timer_get_time();

        /* Optional display fields */
        cJSON *msg_j = cJSON_GetObjectItem(root, "msg");
        if (cJSON_IsString(msg_j))
            strlcpy(e->data.session.msg, msg_j->valuestring,
                    sizeof(e->data.session.msg));

        cJSON *entries_j = cJSON_GetObjectItem(root, "entries");
        if (cJSON_IsArray(entries_j)) {
            int n = cJSON_GetArraySize(entries_j);
            if (n > 8) n = 8;
            for (int i = 0; i < n; i++) {
                cJSON *item = cJSON_GetArrayItem(entries_j, i);
                if (cJSON_IsString(item))
                    strlcpy(e->data.session.entries[i], item->valuestring, 92);
            }
            e->data.session.n_entries = (uint8_t)n;
        }

        /* Debug: dump all heartbeat fields */
        ESP_LOGI(TAG, "HB r=%lu w=%lu tok=%lu tok_today=%lu total=%lu msg='%s' n_entries=%d",
                 (unsigned long)e->data.session.running,
                 (unsigned long)e->data.session.waiting,
                 (unsigned long)e->data.session.tokens_total,
                 (unsigned long)e->data.session.tokens_today,
                 (unsigned long)(total_j ? (uint32_t)total_j->valuedouble : 0),
                 e->data.session.msg,
                 e->data.session.n_entries);

        /* Token milestone */
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

        /* Nested prompt → APPROVAL_REQUEST (only when id changes) */
        cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
        cJSON *id_j   = prompt ? cJSON_GetObjectItem(prompt, "id")   : NULL;
        cJSON *tool_j = prompt ? cJSON_GetObjectItem(prompt, "tool") : NULL;
        cJSON *hint_j = prompt ? cJSON_GetObjectItem(prompt, "hint") : NULL;

        if (id_j && cJSON_IsString(id_j) && id_j->valuestring[0] &&
            strcmp(id_j->valuestring, ctx->last_prompt_id) != 0 &&
            idx < max_events) {

            agent_event_t *pe = &out_events[idx++];
            memset(pe, 0, sizeof(*pe));
            pe->type = AGENT_EVT_APPROVAL_REQUEST;
            strlcpy(pe->data.approval_req.id,   id_j->valuestring,
                    sizeof(pe->data.approval_req.id));
            strlcpy(pe->data.approval_req.tool,
                    (tool_j && cJSON_IsString(tool_j)) ? tool_j->valuestring : "?",
                    sizeof(pe->data.approval_req.tool));
            if (hint_j && cJSON_IsString(hint_j))
                strlcpy(pe->data.approval_req.hint, hint_j->valuestring,
                        sizeof(pe->data.approval_req.hint));
            strlcpy(ctx->last_prompt_id, id_j->valuestring, sizeof(ctx->last_prompt_id));
            pe->timestamp_us = esp_timer_get_time();

            ESP_LOGI(TAG, "approval request: id=%s tool=%s",
                     pe->data.approval_req.id, pe->data.approval_req.tool);
        }

        /* Prompt disappeared → clear dedup cache */
        if (!id_j && ctx->last_prompt_id[0]) {
            ctx->last_prompt_id[0] = '\0';
            ESP_LOGD(TAG, "prompt cleared, cleared dedup cache");
        }
    }

    /* Turn event: {"evt":"turn","role":"assistant"} — Claude completed a response */
    cJSON *evt_j  = cJSON_GetObjectItem(root, "evt");
    cJSON *role_j = cJSON_GetObjectItem(root, "role");
    if (cJSON_IsString(evt_j) && strcmp(evt_j->valuestring, "turn") == 0 &&
        cJSON_IsString(role_j) && strcmp(role_j->valuestring, "assistant") == 0 &&
        idx < max_events) {
        agent_event_t *te = &out_events[idx++];
        memset(te, 0, sizeof(*te));
        te->type = AGENT_EVT_TURN_COMPLETE;
        te->timestamp_us = esp_timer_get_time();
        ESP_LOGI(TAG, "turn complete: assistant response received");
    }

    /* Commands from desktop: {"cmd":"owner"|"name"|"status"|"unpair",...}
     * Generate AGENT_EVT_CMD so agent_core can send the required ack. */
    cJSON *cmd_j = cJSON_GetObjectItem(root, "cmd");
    if (cJSON_IsString(cmd_j) && idx < max_events) {
        agent_event_t *ce = &out_events[idx++];
        memset(ce, 0, sizeof(*ce));
        ce->type = AGENT_EVT_CMD;
        strlcpy(ce->data.cmd.name, cmd_j->valuestring, sizeof(ce->data.cmd.name));
        /* Capture the optional string value (present for "name" and "owner") */
        cJSON *val_j = cJSON_GetObjectItem(root, "name");
        if (cJSON_IsString(val_j))
            strlcpy(ce->data.cmd.value, val_j->valuestring, sizeof(ce->data.cmd.value));
        ce->timestamp_us = esp_timer_get_time();
        ESP_LOGI(TAG, "cmd received: %s", ce->data.cmd.name);
    }

    *n_events = idx;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t claude_buddy_encode(proto_t *proto,
                                      const proto_out_msg_t *msg,
                                      uint8_t **out_data, size_t *out_len)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    switch (msg->type) {
    case PROTO_MSG_APPROVAL_RESPONSE:
        /* {"cmd":"permission","id":"<id>","decision":"once"|"deny"} */
        cJSON_AddStringToObject(root, "cmd", "permission");
        cJSON_AddStringToObject(root, "id",  msg->approval.id);
        cJSON_AddStringToObject(root, "decision",
                                 msg->approval.approved ? "once" : "deny");
        break;

    case PROTO_MSG_HEARTBEAT_ACK:
        cJSON_AddStringToObject(root, "type", "ack");
        break;

    case PROTO_MSG_DEVICE_NAME:
        /* Intentionally empty — device name is already in BLE advertising;
         * no standard protocol message exists for device→desktop name push. */
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;

    case PROTO_MSG_STATUS_REQUEST:
        /* Desktop polls {"cmd":"status"}; device responds via PROTO_MSG_CMD_ACK. */
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;

    case PROTO_MSG_CMD_ACK: {
        cJSON_AddStringToObject(root, "ack", msg->cmd_ack.cmd);
        cJSON_AddBoolToObject(root, "ok", msg->cmd_ack.ok);
        /* For "status", add data block per protocol spec */
        if (strcmp(msg->cmd_ack.cmd, "status") == 0) {
            cJSON *data = cJSON_CreateObject();
            /* Device name — required by desktop for device identification */
            cJSON_AddStringToObject(data, "name",
                                     transport_ble_get_device_name());
            /* Link security status */
            cJSON_AddBoolToObject(data, "sec", transport_ble_is_secure());
            /* System info */
            cJSON *sys = cJSON_CreateObject();
            cJSON_AddNumberToObject(sys, "heap", (double)esp_get_free_heap_size());
            cJSON_AddItemToObject(data, "sys", sys);
            /* Stats — use agent_stats if available */
            cJSON *stats = cJSON_CreateObject();
            cJSON_AddItemToObject(data, "stats", stats);
            cJSON_AddItemToObject(root, "data", data);
            ESP_LOGI(TAG, "status ack: name=%s sec=%d heap=%lu",
                     transport_ble_get_device_name(),
                     transport_ble_is_secure() ? 1 : 0,
                     (unsigned long)esp_get_free_heap_size());
        }
        break;
    }

    case PROTO_MSG_TIME_SYNC: {
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg->time_sync.epoch_s));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg->time_sync.tz_offset_s));
        cJSON_AddItemToObject(root, "time", arr);
        break;
    }
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

static esp_err_t claude_buddy_init(proto_t *proto, const void *cfg)
{
    claude_buddy_ctx_t *ctx = (claude_buddy_ctx_t *)proto;
    ctx->token_milestone  = CONFIG_PROTO_TOKEN_MILESTONE;
    ctx->last_token_total = 0;
    ctx->last_prompt_id[0] = '\0';
    return ESP_OK;
}

static void claude_buddy_deinit(proto_t *proto) { }

esp_err_t proto_claude_buddy_create(proto_t **out)
{
    claude_buddy_ctx_t *ctx = calloc(1, sizeof(claude_buddy_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    ctx->base.name   = "claude_buddy";
    ctx->base.decode = claude_buddy_decode;
    ctx->base.encode = claude_buddy_encode;
    ctx->base.init   = claude_buddy_init;
    ctx->base.deinit = claude_buddy_deinit;
    ctx->base.priv   = ctx;

    claude_buddy_init((proto_t *)ctx, NULL);
    *out = (proto_t *)ctx;
    return ESP_OK;
}

#endif /* CONFIG_PROTO_CLAUDE_BUDDY_ENABLED */
