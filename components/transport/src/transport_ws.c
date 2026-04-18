#include "sdkconfig.h"
#if CONFIG_TRANSPORT_WS_ENABLED

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "transport/transport.h"

#define TAG "WS"
#define RECONNECT_MS_INIT   1000
#define RECONNECT_MS_MAX    30000

typedef struct {
    transport_t                  base;
    char                         server_url[128];
    esp_websocket_client_handle_t client;
    transport_state_t            state;
    uint32_t                     reconnect_ms;
} ws_ctx_t;

static void ws_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    ws_ctx_t *ctx = (ws_ctx_t *)arg;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ctx->state = TRANSPORT_STATE_CONNECTED;
        ctx->reconnect_ms = RECONNECT_MS_INIT;
        ESP_LOGI(TAG, "connected to %s", ctx->server_url);
        if (ctx->base.state_cb)
            ctx->base.state_cb(TRANSPORT_ID_WS, TRANSPORT_STATE_CONNECTED, ctx->base.cb_ctx);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ctx->state = TRANSPORT_STATE_DISCONNECTED;
        ESP_LOGW(TAG, "disconnected, retry in %lums", (unsigned long)ctx->reconnect_ms);
        if (ctx->base.state_cb)
            ctx->base.state_cb(TRANSPORT_ID_WS, TRANSPORT_STATE_DISCONNECTED, ctx->base.cb_ctx);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->data_len > 0 && ctx->base.rx_cb) {
            ctx->base.rx_cb(TRANSPORT_ID_WS,
                             (const uint8_t *)data->data_ptr,
                             data->data_len,
                             ctx->base.cb_ctx);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ctx->state = TRANSPORT_STATE_ERROR;
        ESP_LOGE(TAG, "WebSocket error");
        break;

    default:
        break;
    }
}

static esp_err_t ws_transport_init(transport_t *t, const void *cfg)
{
    ws_ctx_t *ctx = (ws_ctx_t *)t;
    ctx->state = TRANSPORT_STATE_DISCONNECTED;
    ctx->reconnect_ms = RECONNECT_MS_INIT;
    return ESP_OK;
}

static esp_err_t ws_transport_start(transport_t *t)
{
    ws_ctx_t *ctx = (ws_ctx_t *)t;
    esp_websocket_client_config_t ws_cfg = {
        .uri             = ctx->server_url,
        .reconnect_timeout_ms = ctx->reconnect_ms,
        .network_timeout_ms   = 10000,
    };
    ctx->client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ctx->client, WEBSOCKET_EVENT_ANY,
                                   ws_event_handler, ctx);
    return esp_websocket_client_start(ctx->client);
}

static esp_err_t ws_transport_stop(transport_t *t)
{
    ws_ctx_t *ctx = (ws_ctx_t *)t;
    if (ctx->client) {
        esp_websocket_client_stop(ctx->client);
        esp_websocket_client_destroy(ctx->client);
        ctx->client = NULL;
    }
    return ESP_OK;
}

static esp_err_t ws_transport_send(transport_t *t, const uint8_t *data, size_t len)
{
    ws_ctx_t *ctx = (ws_ctx_t *)t;
    if (!ctx->client || ctx->state != TRANSPORT_STATE_CONNECTED)
        return ESP_ERR_INVALID_STATE;
    int sent = esp_websocket_client_send_text(ctx->client, (const char *)data, len, pdMS_TO_TICKS(1000));
    return sent >= 0 ? ESP_OK : ESP_FAIL;
}

static transport_state_t ws_transport_get_state(transport_t *t)
{
    return ((ws_ctx_t *)t)->state;
}

esp_err_t transport_ws_create(transport_t **out, const char *server_url)
{
    ws_ctx_t *ctx = calloc(1, sizeof(ws_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    strlcpy(ctx->server_url,
            server_url ? server_url : CONFIG_TRANSPORT_WS_DEFAULT_URL,
            sizeof(ctx->server_url));
    ctx->base.id        = TRANSPORT_ID_WS;
    ctx->base.init      = ws_transport_init;
    ctx->base.start     = ws_transport_start;
    ctx->base.stop      = ws_transport_stop;
    ctx->base.send      = ws_transport_send;
    ctx->base.get_state = ws_transport_get_state;

    *out = (transport_t *)ctx;
    return ESP_OK;
}

#endif /* CONFIG_TRANSPORT_WS_ENABLED */
