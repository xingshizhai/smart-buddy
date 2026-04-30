#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "buddy_hal/agent_events.h"

typedef struct proto_s proto_t;

typedef enum {
    PROTO_MSG_APPROVAL_RESPONSE = 0,
    PROTO_MSG_STATUS_REQUEST,
    PROTO_MSG_DEVICE_NAME,
    PROTO_MSG_HEARTBEAT_ACK,
    PROTO_MSG_TIME_SYNC,
    PROTO_MSG_CMD_ACK,      /* {"ack":"<cmd>","ok":true[,"data":{...}]} */
} proto_msg_type_t;

typedef struct {
    proto_msg_type_t type;
    union {
        struct {
            char id[64];
            bool approved;
        } approval;
        struct {
            char name[32];
        } device;
        struct {
            int64_t epoch_s;
            int32_t tz_offset_s;
        } time_sync;
        struct {
            char cmd[32];   /* the command being acked: "owner", "name", "status", "unpair" */
            bool ok;
        } cmd_ack;
    };
} proto_out_msg_t;

struct proto_s {
    const char *name;
    esp_err_t (*decode)(proto_t *proto,
                        const uint8_t *data, size_t len,
                        agent_event_t *out_events,
                        size_t max_events,
                        size_t *n_events);
    esp_err_t (*encode)(proto_t *proto,
                        const proto_out_msg_t *msg,
                        uint8_t **out_data, size_t *out_len);
    esp_err_t (*init)(proto_t *proto, const void *cfg);
    void      (*deinit)(proto_t *proto);
    void      *priv;
};

/* Factory functions */
esp_err_t proto_claude_buddy_create(proto_t **out);
esp_err_t proto_openclaw_create(proto_t **out);
esp_err_t proto_hermes_create(proto_t **out);

esp_err_t proto_register(proto_t *proto);
proto_t  *proto_get_active(void);
esp_err_t proto_set_active(const char *name);
