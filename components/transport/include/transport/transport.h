#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define TRANSPORT_MAX_FRAME_SIZE  4096
#define TRANSPORT_MAX_INSTANCES   3

typedef enum {
    TRANSPORT_ID_BLE = 0,
    TRANSPORT_ID_WS  = 1,
    TRANSPORT_ID_USB = 2,
} transport_id_t;

typedef enum {
    TRANSPORT_STATE_DISCONNECTED = 0,
    TRANSPORT_STATE_CONNECTING,
    TRANSPORT_STATE_CONNECTED,
    TRANSPORT_STATE_ERROR,
} transport_state_t;

typedef void (*transport_rx_cb_t)(transport_id_t id,
                                   const uint8_t *data,
                                   size_t len,
                                   void *ctx);
typedef void (*transport_state_cb_t)(transport_id_t id,
                                      transport_state_t state,
                                      void *ctx);

typedef struct transport_s transport_t;

struct transport_s {
    transport_id_t id;
    esp_err_t (*init)(transport_t *t, const void *cfg);
    esp_err_t (*deinit)(transport_t *t);
    esp_err_t (*start)(transport_t *t);
    esp_err_t (*stop)(transport_t *t);
    esp_err_t (*send)(transport_t *t, const uint8_t *data, size_t len);
    transport_state_t (*get_state)(transport_t *t);

    transport_rx_cb_t    rx_cb;
    transport_state_cb_t state_cb;
    void                *cb_ctx;
    void                *priv;
};

/* Factory functions implemented in each transport source file */
esp_err_t transport_ble_create(transport_t **out, const char *device_name, uint16_t mtu);
esp_err_t transport_ws_create(transport_t **out, const char *url);
esp_err_t transport_usb_create(transport_t **out);

/* BLE-specific accessors (safe to call even when disconnected) */
esp_err_t transport_ble_get_mac(uint8_t mac[6]);
uint16_t  transport_ble_get_mtu(void);

esp_err_t         transport_register(transport_t *t);
esp_err_t         transport_start_all(void);
esp_err_t         transport_stop_all(void);
esp_err_t         transport_send(transport_id_t id, const uint8_t *data, size_t len);
esp_err_t         transport_send_all(const uint8_t *data, size_t len);
transport_state_t transport_get_state(transport_id_t id);
void              transport_set_callbacks(transport_rx_cb_t rx_cb,
                                          transport_state_cb_t state_cb,
                                          void *ctx);
