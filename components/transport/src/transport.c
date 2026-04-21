#include <string.h>
#include "esp_log.h"
#include "transport/transport.h"

#define TAG "TRANSPORT"

static transport_t       *s_transports[TRANSPORT_MAX_INSTANCES] = {0};
static transport_rx_cb_t  s_rx_cb    = NULL;
static transport_state_cb_t s_state_cb = NULL;
static void              *s_cb_ctx   = NULL;

void transport_set_callbacks(transport_rx_cb_t rx_cb,
                              transport_state_cb_t state_cb,
                              void *ctx)
{
    s_rx_cb    = rx_cb;
    s_state_cb = state_cb;
    s_cb_ctx   = ctx;
    /* Back-fill already-registered transports — handles the case where
     * transports are registered before agent_core calls this function. */
    for (int i = 0; i < TRANSPORT_MAX_INSTANCES; i++) {
        if (s_transports[i]) {
            s_transports[i]->rx_cb    = rx_cb;
            s_transports[i]->state_cb = state_cb;
            s_transports[i]->cb_ctx   = ctx;
        }
    }
}

esp_err_t transport_register(transport_t *t)
{
    if (!t || t->id >= TRANSPORT_MAX_INSTANCES) return ESP_ERR_INVALID_ARG;
    t->rx_cb    = s_rx_cb;
    t->state_cb = s_state_cb;
    t->cb_ctx   = s_cb_ctx;
    s_transports[t->id] = t;
    ESP_LOGI(TAG, "registered transport id=%d", t->id);
    return ESP_OK;
}

esp_err_t transport_start_all(void)
{
    for (int i = 0; i < TRANSPORT_MAX_INSTANCES; i++) {
        transport_t *t = s_transports[i];
        if (!t) continue;
        if (t->init) {
            esp_err_t r = t->init(t, NULL);
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "transport %d init failed: %s", i, esp_err_to_name(r));
                continue;
            }
        }
        if (t->start) {
            esp_err_t r = t->start(t);
            if (r != ESP_OK)
                ESP_LOGW(TAG, "transport %d start failed: %s", i, esp_err_to_name(r));
        }
    }
    return ESP_OK;
}

esp_err_t transport_stop_all(void)
{
    for (int i = 0; i < TRANSPORT_MAX_INSTANCES; i++) {
        if (s_transports[i] && s_transports[i]->stop)
            s_transports[i]->stop(s_transports[i]);
    }
    return ESP_OK;
}

esp_err_t transport_send(transport_id_t id, const uint8_t *data, size_t len)
{
    if (id >= TRANSPORT_MAX_INSTANCES || !s_transports[id])
        return ESP_ERR_INVALID_ARG;
    return s_transports[id]->send(s_transports[id], data, len);
}

esp_err_t transport_send_all(const uint8_t *data, size_t len)
{
    for (int i = 0; i < TRANSPORT_MAX_INSTANCES; i++) {
        if (!s_transports[i]) continue;
        transport_state_t st = s_transports[i]->get_state(s_transports[i]);
        if (st == TRANSPORT_STATE_CONNECTED)
            s_transports[i]->send(s_transports[i], data, len);
    }
    return ESP_OK;
}

transport_state_t transport_get_state(transport_id_t id)
{
    if (id >= TRANSPORT_MAX_INSTANCES || !s_transports[id])
        return TRANSPORT_STATE_DISCONNECTED;
    return s_transports[id]->get_state(s_transports[id]);
}
