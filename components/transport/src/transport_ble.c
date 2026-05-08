#include "sdkconfig.h"
#if CONFIG_TRANSPORT_BLE_ENABLED

#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "transport/transport.h"

#define TAG "BLE"

/*
 * Nordic UART Service (NUS) UUIDs in little-endian byte order.
 * Base UUID: 6E40xxxx-B5A3-F393-E0A9-E50E24DCCA9E
 */
#define NUS_SVC_UUID_BYTES  \
    0x9E,0xCA,0xDC,0x24, 0x0E,0xE5, 0xA9,0xE0, \
    0x93,0xF3, 0xA3,0xB5, 0x01,0x00,0x40,0x6E

#define NUS_RX_UUID_BYTES   \
    0x9E,0xCA,0xDC,0x24, 0x0E,0xE5, 0xA9,0xE0, \
    0x93,0xF3, 0xA3,0xB5, 0x02,0x00,0x40,0x6E

#define NUS_TX_UUID_BYTES   \
    0x9E,0xCA,0xDC,0x24, 0x0E,0xE5, 0xA9,0xE0, \
    0x93,0xF3, 0xA3,0xB5, 0x03,0x00,0x40,0x6E

/*
 * Standard NUS characteristic flags — no encryption required on the service.
 * macOS (Claude Desktop) does not perform passkey entry, so requiring
 * BLE_GATT_CHR_F_WRITE_ENC / BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC would cause
 * every CCCD write to fail with Insufficient Auth → pairing → MITM failure
 * → disconnect.  Leave the service open and let the SM layer handle optional
 * Just-Works bonding independently.
 */
#define NUS_RX_CHR_FLAGS (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)
#define NUS_TX_CHR_FLAGS (BLE_GATT_CHR_F_NOTIFY)

typedef struct {
    transport_t         base;
    char                device_name[32];
    uint16_t            mtu;
    uint16_t            conn_handle;
    uint16_t            tx_attr_handle;
    transport_state_t   state;
    bool                cccd_subscribed;  /* central has enabled TX notifications */
    char                rx_buf[TRANSPORT_MAX_FRAME_SIZE];
    size_t              rx_pos;
} ble_ctx_t;

static ble_ctx_t *s_ctx = NULL;
/* Static handle written by NimBLE GATT registration */
static uint16_t   s_tx_attr_handle = 0;

/* Security / passkey state */
static uint32_t s_passkey = 0;
static bool     s_secure  = false;

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void ble_start_advertising(void);

static int nus_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI(TAG, "chr access op=%d attr=%d", ctxt->op, attr_handle);
    if (!s_ctx) return BLE_ATT_ERR_UNLIKELY;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        uint8_t *buf = malloc(len);
        if (!buf) return BLE_ATT_ERR_INSUFFICIENT_RES;
        ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
        ESP_LOGI(TAG, "RX %u bytes: %.*s", len, (int)len, (char *)buf);

        for (uint16_t i = 0; i < len; i++) {
            if (s_ctx->rx_pos < sizeof(s_ctx->rx_buf) - 1)
                s_ctx->rx_buf[s_ctx->rx_pos++] = buf[i];
            if (buf[i] == '\n') {
                s_ctx->rx_buf[s_ctx->rx_pos] = '\0';
                if (s_ctx->base.rx_cb)
                    s_ctx->base.rx_cb(TRANSPORT_ID_BLE,
                                      (uint8_t *)s_ctx->rx_buf,
                                      s_ctx->rx_pos,
                                      s_ctx->base.cb_ctx);
                s_ctx->rx_pos = 0;
            }
        }
        free(buf);
    }
    return 0;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(NUS_SVC_UUID_BYTES),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = BLE_UUID128_DECLARE(NUS_RX_UUID_BYTES),
                .access_cb = nus_chr_access_cb,
                .flags     = NUS_RX_CHR_FLAGS,
            },
            {
                .uuid       = BLE_UUID128_DECLARE(NUS_TX_UUID_BYTES),
                .access_cb  = nus_chr_access_cb,
                .val_handle = &s_tx_attr_handle,
                .flags      = NUS_TX_CHR_FLAGS,
            },
            { 0 }
        },
    },
    { 0 }
};

static void ble_on_sync(void)
{
    esp_log_level_set("NimBLE", ESP_LOG_DEBUG);
    ble_hs_util_ensure_addr(0);
    ble_att_set_preferred_mtu(512);

    /* Open NUS — no bonding, no pairing required. */
    ble_hs_cfg.sm_io_cap  = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm    = 0;
    ble_hs_cfg.sm_sc      = 0;

    ble_start_advertising();
}

static void ble_start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    /*
     * ADV data: flags + NUS service UUID (Claude app scans by this UUID).
     * Scan response: complete device name (must start with "Claude").
     * Splitting is necessary because a 128-bit UUID + flags already fills
     * the 31-byte ADV payload, leaving no room for the name.
     */
    ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(NUS_SVC_UUID_BYTES);
    struct ble_hs_adv_fields adv_fields = {
        .flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128             = &nus_svc_uuid,
        .num_uuids128         = 1,
        .uuids128_is_complete = 1,
    };
    ble_gap_adv_set_fields(&adv_fields);

    const char *name = s_ctx ? s_ctx->device_name : CONFIG_TRANSPORT_BLE_DEVICE_NAME;
    struct ble_hs_adv_fields rsp_fields = {
        .name             = (const uint8_t *)name,
        .name_len         = strlen(name),
        .name_is_complete = 1,
    };
    ble_gap_adv_rsp_set_fields(&rsp_fields);

    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                      ble_gap_event_cb, NULL);
    ESP_LOGI(TAG, "advertising as \"%s\" with NUS UUID", name);
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    if (!s_ctx) return 0;
    switch (event->type) {
    case BLE_GAP_EVENT_SUBSCRIBE:
        /* Central subscribed/unsubscribed to TX notifications */
        ESP_LOGI(TAG, "subscribe attr=%d notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        if (event->subscribe.attr_handle == s_tx_attr_handle &&
            s_ctx->state == TRANSPORT_STATE_CONNECTED) {
            if (event->subscribe.cur_notify) {
                s_ctx->cccd_subscribed = true;
                /* Re-fire connected so agent_core sends time_sync now that
                 * notifications are permitted. Guard on state == CONNECTED
                 * prevents a stale SUBSCRIBE after disconnect. */
                if (s_ctx->base.state_cb)
                    s_ctx->base.state_cb(TRANSPORT_ID_BLE, TRANSPORT_STATE_CONNECTED,
                                         s_ctx->base.cb_ctx);
            } else {
                s_ctx->cccd_subscribed = false;
            }
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_ctx->conn_handle = event->connect.conn_handle;
            s_ctx->state = TRANSPORT_STATE_CONNECTED;
            s_ctx->cccd_subscribed = false;
            ESP_LOGI(TAG, "connected handle=%d", s_ctx->conn_handle);
            /* Notify at physical connect so the UI wakes (screen on, BLE indicator
             * green).  Data sending is guarded by cccd_subscribed in ble_tp_send,
             * so no notifications reach macOS before the central subscribes. */
            if (s_ctx->base.state_cb)
                s_ctx->base.state_cb(TRANSPORT_ID_BLE, TRANSPORT_STATE_CONNECTED,
                                     s_ctx->base.cb_ctx);
        } else {
            ble_start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        s_ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ctx->state = TRANSPORT_STATE_DISCONNECTED;
        s_ctx->cccd_subscribed = false;
        s_secure  = false;
        s_passkey = 0;
        if (s_ctx->base.state_cb)
            s_ctx->base.state_cb(TRANSPORT_ID_BLE, TRANSPORT_STATE_DISCONNECTED,
                                 s_ctx->base.cb_ctx);
        ble_start_advertising();
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        if (s_ctx) s_ctx->mtu = event->mtu.value;
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        /*
         * LE Secure Connections pairing with DISPLAY_ONLY capability.
         * The application MUST generate a random 6-digit passkey, display it,
         * and inject it via ble_sm_inject_io(). The user then types it on the
         * desktop (KeyboardOnly on the central side).
         */
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            s_passkey = esp_random() % 1000000;
            struct ble_sm_io io = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = s_passkey,
            };
            ble_sm_inject_io(event->passkey.conn_handle, &io);
            ESP_LOGI(TAG, "=== PASSKEY: %06lu (display on screen) ===",
                     (unsigned long)s_passkey);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            /* Numeric comparison - auto-accept for peripheral role */
            struct ble_sm_io io = {
                .action = BLE_SM_IOACT_NUMCMP,
                .numcmp_accept = 1,
            };
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        break;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        s_secure = (event->enc_change.status == 0);
        ESP_LOGI(TAG, "encryption %s (status=%d)",
                 s_secure ? "OK" : "FAIL", event->enc_change.status);
        s_passkey = 0;
        break;

    default:
        ESP_LOGD(TAG, "gap event %d", event->type);
        break;
    }
    return 0;
}

bool transport_ble_is_secure(void)
{
    return s_secure;
}

uint32_t transport_ble_get_passkey(void)
{
    return s_passkey;
}

static esp_err_t ble_tp_init(transport_t *t, const void *cfg)
{
    ble_ctx_t *ctx = (ble_ctx_t *)t;
    ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ctx->state = TRANSPORT_STATE_DISCONNECTED;
    ctx->cccd_subscribed = false;
    return ESP_OK;
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t ble_tp_start(transport_t *t)
{
    esp_err_t rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return rc;
    }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);
    ble_hs_cfg.sync_cb = ble_on_sync;
    if (s_ctx) ble_svc_gap_device_name_set(s_ctx->device_name);
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

static esp_err_t ble_tp_stop(transport_t *t)
{
    nimble_port_freertos_deinit();
    nimble_port_deinit();
    return ESP_OK;
}

static esp_err_t ble_tp_send(transport_t *t, const uint8_t *data, size_t len)
{
    ble_ctx_t *ctx = (ble_ctx_t *)t;
    if (ctx->state != TRANSPORT_STATE_CONNECTED) return ESP_ERR_INVALID_STATE;
    if (!ctx->cccd_subscribed) return ESP_ERR_INVALID_STATE;

    size_t mtu = ctx->mtu > 20 ? ctx->mtu - 3 : 20;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = (len - offset) < mtu ? (len - offset) : mtu;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + offset, chunk);
        if (!om) return ESP_ERR_NO_MEM;
        ble_gattc_notify_custom(ctx->conn_handle, s_tx_attr_handle, om);
        offset += chunk;
    }
    return ESP_OK;
}

static transport_state_t ble_tp_get_state(transport_t *t)
{
    return ((ble_ctx_t *)t)->state;
}

esp_err_t transport_ble_create(transport_t **out, const char *device_name, uint16_t mtu)
{
    ble_ctx_t *ctx = calloc(1, sizeof(ble_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    if (device_name && device_name[0]) {
        strlcpy(ctx->device_name, device_name, sizeof(ctx->device_name));
    } else {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(ctx->device_name, sizeof(ctx->device_name),
                 "Claude-%02X%02X", mac[4], mac[5]);
    }
    ctx->mtu = mtu ? mtu : 512;
    ctx->base.id        = TRANSPORT_ID_BLE;
    ctx->base.init      = ble_tp_init;
    ctx->base.start     = ble_tp_start;
    ctx->base.stop      = ble_tp_stop;
    ctx->base.send      = ble_tp_send;
    ctx->base.get_state = ble_tp_get_state;

    s_ctx = ctx;
    *out  = (transport_t *)ctx;
    return ESP_OK;
}

esp_err_t transport_ble_get_mac(uint8_t mac[6])
{
    uint8_t addr[6];
    uint8_t addr_type;
    int rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc != 0) return ESP_FAIL;
    rc = ble_hs_id_copy_addr(addr_type, addr, NULL);
    if (rc != 0) return ESP_FAIL;
    /* NimBLE stores address in little-endian; reverse to display order */
    for (int i = 0; i < 6; i++) mac[i] = addr[5 - i];
    return ESP_OK;
}

uint16_t transport_ble_get_mtu(void)
{
    return s_ctx ? s_ctx->mtu : 23;
}

const char *transport_ble_get_device_name(void)
{
    return s_ctx ? s_ctx->device_name : CONFIG_TRANSPORT_BLE_DEVICE_NAME;
}

#endif /* CONFIG_TRANSPORT_BLE_ENABLED */
