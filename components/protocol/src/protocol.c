#include <string.h>
#include "esp_log.h"
#include "buddy_hal/hal_storage.h"
#include "protocol/protocol.h"

#define TAG "PROTO"
#define MAX_PROTOS 4

static proto_t *s_protos[MAX_PROTOS] = {0};
static proto_t *s_active = NULL;
static int      s_count  = 0;

esp_err_t proto_register(proto_t *proto)
{
    if (!proto || s_count >= MAX_PROTOS) return ESP_ERR_INVALID_ARG;
    s_protos[s_count++] = proto;
    ESP_LOGI(TAG, "registered protocol: %s", proto->name);
    return ESP_OK;
}

proto_t *proto_get_active(void)
{
    return s_active;
}

esp_err_t proto_set_active(const char *name)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_protos[i]->name, name) == 0) {
            s_active = s_protos[i];
            hal_storage_set_str("proto_active", name);
            ESP_LOGI(TAG, "active protocol: %s", name);
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "protocol not found: %s", name);
    return ESP_ERR_NOT_FOUND;
}
