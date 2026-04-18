#include "sdkconfig.h"
#if CONFIG_TRANSPORT_USB_ENABLED

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "transport/transport.h"

#define TAG         "USB"
#define RX_BUF_SIZE 4096
#define TX_BUF_SIZE 4096
#define TASK_STACK  4096
#define TASK_PRIO   4

typedef struct {
    transport_t       base;
    transport_state_t state;
    TaskHandle_t      rx_task;
    char              rx_buf[TRANSPORT_MAX_FRAME_SIZE];
    size_t            rx_pos;
} usb_ctx_t;

static usb_ctx_t *s_ctx = NULL;

static void usb_rx_task(void *arg)
{
    uint8_t ch;
    for (;;) {
        int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100));
        if (n <= 0) continue;
        if (s_ctx->rx_pos < sizeof(s_ctx->rx_buf) - 1)
            s_ctx->rx_buf[s_ctx->rx_pos++] = ch;
        if (ch == '\n') {
            s_ctx->rx_buf[s_ctx->rx_pos] = '\0';
            if (s_ctx->base.rx_cb)
                s_ctx->base.rx_cb(TRANSPORT_ID_USB,
                                  (uint8_t *)s_ctx->rx_buf,
                                  s_ctx->rx_pos,
                                  s_ctx->base.cb_ctx);
            s_ctx->rx_pos = 0;
        }
    }
}

static esp_err_t usb_tp_init(transport_t *t, const void *cfg)
{
    usb_serial_jtag_driver_config_t dcfg = {
        .rx_buffer_size = RX_BUF_SIZE,
        .tx_buffer_size = TX_BUF_SIZE,
    };
    esp_err_t r = usb_serial_jtag_driver_install(&dcfg);
    if (r == ESP_ERR_INVALID_STATE) r = ESP_OK;
    return r;
}

static esp_err_t usb_tp_start(transport_t *t)
{
    usb_ctx_t *ctx = (usb_ctx_t *)t;
    ctx->state = TRANSPORT_STATE_CONNECTED;
    if (ctx->base.state_cb)
        ctx->base.state_cb(TRANSPORT_ID_USB, TRANSPORT_STATE_CONNECTED, ctx->base.cb_ctx);
    xTaskCreate(usb_rx_task, "usb_rx", TASK_STACK, NULL, TASK_PRIO, &ctx->rx_task);
    return ESP_OK;
}

static esp_err_t usb_tp_stop(transport_t *t)
{
    usb_ctx_t *ctx = (usb_ctx_t *)t;
    if (ctx->rx_task) { vTaskDelete(ctx->rx_task); ctx->rx_task = NULL; }
    return ESP_OK;
}

static esp_err_t usb_tp_send(transport_t *t, const uint8_t *data, size_t len)
{
    int written = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(100));
    return ((size_t)written == len) ? ESP_OK : ESP_FAIL;
}

static transport_state_t usb_tp_get_state(transport_t *t)
{
    return ((usb_ctx_t *)t)->state;
}

esp_err_t transport_usb_create(transport_t **out)
{
    usb_ctx_t *ctx = calloc(1, sizeof(usb_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    ctx->state          = TRANSPORT_STATE_DISCONNECTED;
    ctx->base.id        = TRANSPORT_ID_USB;
    ctx->base.init      = usb_tp_init;
    ctx->base.start     = usb_tp_start;
    ctx->base.stop      = usb_tp_stop;
    ctx->base.send      = usb_tp_send;
    ctx->base.get_state = usb_tp_get_state;

    s_ctx = ctx;
    *out  = (transport_t *)ctx;
    return ESP_OK;
}

#endif /* CONFIG_TRANSPORT_USB_ENABLED */
