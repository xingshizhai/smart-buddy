#include "sdkconfig.h"
#if CONFIG_BT_NIMBLE_ENABLED

#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs_pvcy.h"
#include "host/ble_sm.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/util/util.h"
#include "transport/transport.h"

/* ble_store_config_init is not exported in the public header */
void ble_store_config_init(void);

#define TAG "BLE"

typedef struct {
    transport_t         base;
    char                device_name[32];
    uint16_t            mtu;
    uint16_t            conn_handle;
    uint16_t            tx_val_handle;
    uint16_t            rx_val_handle;
    transport_state_t   state;
    bool                cccd_subscribed;
    bool                secure;
    char                rx_buf[TRANSPORT_MAX_FRAME_SIZE];
    size_t              rx_pos;
} ble_ctx_t;

static ble_ctx_t *s_ctx = NULL;
static uint32_t s_passkey = 0;

static void start_advertising(void);
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);

/* ── NUS UUIDs ────────────────────────────────────────────────────── */

static const ble_uuid128_t nus_svc_uuid = {
    .u.type = BLE_UUID_TYPE_128,
    .value = { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
               0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e }
};
static const ble_uuid128_t nus_tx_uuid = {
    .u.type = BLE_UUID_TYPE_128,
    .value = { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
               0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e }
};
static const ble_uuid128_t nus_rx_uuid = {
    .u.type = BLE_UUID_TYPE_128,
    .value = { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
               0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e }
};

/* ── Advertising ────────────────────────────────────────────────────── */

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Adv packet: flags + NUS UUID + shortened name "Claude".
     * Budget: 3 (flags) + 18 (uuid128) + 8 ("Claude" + 2 overhead) = 29 B.
     * The shortened name ensures macOS passive-scan sees "claude…" even
     * before receiving the scan response, satisfying Claude Desktop's
     * c.startsWith("claude") filter. */
    fields.uuids128 = (ble_uuid128_t *)&nus_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    fields.name = (const uint8_t *)"Claude";
    fields.name_len = 6;
    fields.name_is_complete = 0;   /* AD type 0x08 = Shortened Local Name */

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields failed: rc=%d (packet too large?)", rc);
        return;
    }
    ESP_LOGI(TAG, "adv fields OK: flags=0x%02x uuid128_count=%d",
             fields.flags, fields.num_uuids128);

    /* Scan response: full unique name "ClaudeXXYY". */
    const char *name = ble_svc_gap_device_name();
    if (name && name[0]) {
        struct ble_hs_adv_fields rsp;
        memset(&rsp, 0, sizeof(rsp));
        rsp.name = (const uint8_t *)name;
        rsp.name_len = strlen(name);
        rsp.name_is_complete = 1;
        rc = ble_gap_adv_rsp_set_fields(&rsp);
        if (rc != 0) {
            ESP_LOGE(TAG, "adv rsp set fields: %d", rc);
            return;
        }
        ESP_LOGI(TAG, "device name: '%s' (len=%d)", name, rsp.name_len);
    }

    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode  = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode  = BLE_GAP_DISC_MODE_GEN;
    /* 100 ms interval — fast enough for reliable discovery without burning power */
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(100);

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
    if (rc != 0) { ESP_LOGE(TAG, "adv start: %d", rc); return; }
    ESP_LOGI(TAG, "advertising as '%s'", name ? name : "unknown");
}

/* ── GAP event handler ─────────────────────────────────────────────── */

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    if (!s_ctx) return 0;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_ctx->conn_handle = event->connect.conn_handle;
            s_ctx->state = TRANSPORT_STATE_CONNECTED;
            s_ctx->secure = false;
            s_ctx->cccd_subscribed = false;
            s_passkey = 0;
            ESP_LOGI(TAG, "connected conn_handle=%d", s_ctx->conn_handle);
            if (s_ctx->base.state_cb)
                s_ctx->base.state_cb(TRANSPORT_ID_BLE, TRANSPORT_STATE_CONNECTED,
                                     s_ctx->base.cb_ctx);
            /* Initiate encryption (will use existing bond if available) */
            ble_gap_security_initiate(event->connect.conn_handle);
        } else {
            ESP_LOGE(TAG, "connect failed rc=%d", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        s_ctx->state = TRANSPORT_STATE_DISCONNECTED;
        s_ctx->secure = false;
        s_ctx->cccd_subscribed = false;
        s_passkey = 0;
        if (s_ctx->base.state_cb)
            s_ctx->base.state_cb(TRANSPORT_ID_BLE, TRANSPORT_STATE_DISCONNECTED,
                                 s_ctx->base.cb_ctx);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "adv complete, restarting");
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            s_ctx->secure = true;
            ESP_LOGI(TAG, "encryption OK");
            if (s_ctx->base.state_cb)
                s_ctx->base.state_cb(TRANSPORT_ID_BLE, TRANSPORT_STATE_CONNECTED,
                                     s_ctx->base.cb_ctx);
        } else {
            ESP_LOGE(TAG, "encryption FAIL status=%d", event->enc_change.status);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* Delete old bond for this peer, allow fresh pairing */
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
            ESP_LOGI(TAG, "deleted old bond for repeat pairing");
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            s_passkey = (esp_random() % 1000000);
            struct ble_sm_io pkey = {0};
            pkey.action = BLE_SM_IOACT_DISP;
            pkey.passkey = s_passkey;
            ESP_LOGI(TAG, "=== PASSKEY: %06lu ===", (unsigned long)pkey.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io pkey = {0};
            pkey.action = BLE_SM_IOACT_NUMCMP;
            pkey.numcmp_accept = 1;
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
            /* Just-works pairing */
            ESP_LOGI(TAG, "just-works pairing (no MITM)");
        }
        return 0;
    }

    case BLE_GAP_EVENT_MTU:
        s_ctx->mtu = event->mtu.value;
        ESP_LOGI(TAG, "MTU=%d", s_ctx->mtu);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* NimBLE reports the characteristic VALUE handle in attr_handle, not the CCCD handle. */
        if (event->subscribe.attr_handle == s_ctx->tx_val_handle) {
            s_ctx->cccd_subscribed = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "TX notify %s", s_ctx->cccd_subscribed ? "on" : "off");
        }
        return 0;

    default:
        return 0;
    }
}

/* ── GATT access callback ─────────────────────────────────────────── */

static int gatts_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (!s_ctx) return BLE_ATT_ERR_UNLIKELY;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR &&
        attr_handle == s_ctx->rx_val_handle) {
        ESP_LOGI(TAG, "RX %d bytes", ctxt->om->om_len);
        for (size_t i = 0; i < ctxt->om->om_len; i++) {
            if (s_ctx->rx_pos < sizeof(s_ctx->rx_buf) - 1)
                s_ctx->rx_buf[s_ctx->rx_pos++] = ctxt->om->om_data[i];
            if (ctxt->om->om_data[i] == '\n') {
                s_ctx->rx_buf[s_ctx->rx_pos] = '\0';
                if (s_ctx->base.rx_cb)
                    s_ctx->base.rx_cb(TRANSPORT_ID_BLE,
                                      (uint8_t *)s_ctx->rx_buf,
                                      s_ctx->rx_pos,
                                      s_ctx->base.cb_ctx);
                s_ctx->rx_pos = 0;
            }
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ── GATT service definition ──────────────────────────────────────── */

static uint16_t s_tx_val_handle = 0;
static uint16_t s_rx_val_handle = 0;

static const struct ble_gatt_svc_def nus_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &nus_tx_uuid.u,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_val_handle,
                .access_cb = gatts_access_cb,
            },
            {
                .uuid = &nus_rx_uuid.u,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_rx_val_handle,
                .access_cb = gatts_access_cb,
            },
            {
                .uuid = NULL,
            },
        },
    },
    {
        .type = 0,
    },
};

/* ── Host task ────────────────────────────────────────────────────── */

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_hs_sync_cb(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure addr failed: %d", rc);
    }
    /* NimBLE assigns GATT handles during host startup, before sync_cb fires.
     * Copy them here — copying earlier (in ble_tp_start) reads 0s. */
    if (s_ctx) {
        s_ctx->tx_val_handle = s_tx_val_handle;
        s_ctx->rx_val_handle = s_rx_val_handle;
        ESP_LOGI(TAG, "GATT handles: tx=%d rx=%d",
                 s_ctx->tx_val_handle, s_ctx->rx_val_handle);
    }
    ESP_LOGI(TAG, "BLE synced, starting advertising");
    start_advertising();
}

/* ── Transport interface ──────────────────────────────────────────── */

static esp_err_t ble_tp_init(transport_t *t, const void *cfg)
{
    ble_ctx_t *ctx = (ble_ctx_t *)t;
    ctx->state = TRANSPORT_STATE_DISCONNECTED;
    ctx->cccd_subscribed = false;
    ctx->secure = false;
    ctx->mtu = 23;
    ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    return ESP_OK;
}

static esp_err_t ble_tp_start(transport_t *t)
{
    /* Initialize NVS for BLE bonding */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Step 1: Initialize NimBLE host */
    nimble_port_init();

    /* Step 2: Configure host callbacks */
    ble_hs_cfg.sync_cb = ble_hs_sync_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* macOS / Claude Desktop does not support passkey entry; use Just-Works. */
    ble_hs_cfg.sm_io_cap  = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_mitm    = 0;
    ble_hs_cfg.sm_sc      = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /* Step 3: Initialize GAP/GATT services, register custom services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(nus_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_count_cfg: %d", rc); return ESP_FAIL; }

    rc = ble_gatts_add_svcs(nus_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_add_svcs: %d", rc); return ESP_FAIL; }

    /* Step 4: Set device name AFTER GATT services registered */
    ble_svc_gap_device_name_set(s_ctx->device_name);

    /* Step 5: Initialize NVS storage for bonding */
    ble_store_config_init();

    /* Note: val handles are synced in ble_hs_sync_cb() after NimBLE assigns them. */

    /* Step 6: Start host task (triggers sync_cb → advertising) */
    nimble_port_freertos_init(ble_host_task);

    return ESP_OK;
}

static esp_err_t ble_tp_stop(transport_t *t)
{
    nimble_port_stop();
    return ESP_OK;
}

static esp_err_t ble_tp_send(transport_t *t, const uint8_t *data, size_t len)
{
    ble_ctx_t *ctx = (ble_ctx_t *)t;
    if (ctx->state != TRANSPORT_STATE_CONNECTED) return ESP_ERR_INVALID_STATE;
    if (!ctx->cccd_subscribed) return ESP_ERR_INVALID_STATE;
    if (ctx->tx_val_handle == 0) return ESP_ERR_INVALID_STATE;

    size_t chunk = (ctx->mtu > 3) ? ctx->mtu - 3 : 20;
    if (chunk > 180) chunk = 180;
    size_t offset = 0;
    while (offset < len) {
        size_t n = (len - offset < chunk) ? (len - offset) : chunk;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + offset, n);
        int rc = ble_gattc_notify_custom(ctx->conn_handle, ctx->tx_val_handle, om);
        if (rc != 0) { ESP_LOGE(TAG, "notify failed: %d", rc); return ESP_FAIL; }
        offset += n;
        if (offset < len) vTaskDelay(pdMS_TO_TICKS(4));
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
        /* Short name to fit in 31-byte adv data:
         * flags(2) + 128bit UUID(18) + name overhead(2) = 22 bytes used.
         * Only 9 chars max for device name (total must be <= 31). */
        snprintf(ctx->device_name, sizeof(ctx->device_name),
                 "Claude%02X%02X", mac[4], mac[5]);
    }
    ctx->mtu = mtu ? mtu : 517;
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
    return esp_read_mac(mac, ESP_MAC_BT);
}

uint16_t transport_ble_get_mtu(void)
{
    return s_ctx ? s_ctx->mtu : 23;
}

const char *transport_ble_get_device_name(void)
{
    return s_ctx ? s_ctx->device_name : CONFIG_TRANSPORT_BLE_DEVICE_NAME;
}

bool transport_ble_is_secure(void)
{
    return s_ctx ? s_ctx->secure : false;
}

uint32_t transport_ble_get_passkey(void)
{
    return s_passkey;
}

#endif /* CONFIG_BT_NIMBLE_ENABLED */
