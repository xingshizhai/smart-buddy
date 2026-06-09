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
#include "nvs.h"
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
#define BLE_SCAN_RSP_MAX_NAME_LEN 29

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

/* Deferred state callback — fired asynchronously via esp_timer to avoid
 * calling into the upper layer (agent_core → state_machine → LVGL) from
 * within the NimBLE host task's stack, which caused stack overflow. */
static esp_timer_handle_t s_state_timer = NULL;
static transport_id_t     s_pending_state_id = 0;
static transport_state_t  s_pending_state    = TRANSPORT_STATE_DISCONNECTED;
static void *             s_pending_state_ctx = NULL;

static void deferred_state_cb(void *arg)
{
    if (s_ctx && s_ctx->base.state_cb)
        s_ctx->base.state_cb(s_pending_state_id, s_pending_state, s_pending_state_ctx);
}

static void fire_state_cb_async(transport_id_t id, transport_state_t state, void *ctx)
{
    s_pending_state_id  = id;
    s_pending_state     = state;
    s_pending_state_ctx = ctx;
    esp_timer_stop(s_state_timer);         /* no-op if not running; safe to ignore error */
    esp_timer_start_once(s_state_timer, 0);
}

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

/* Duration (ms) of Low Duty Cycle Directed Advertising before falling back
 * to undirected.  10 s is enough for the Central to wake and connect. */
#define DIRECTED_ADV_DURATION_MS  10000

/* Set to true once directed adv has timed out; cleared on connect/disconnect
 * so subsequent disconnects trigger a fresh directed-adv phase. */
static bool s_directed_adv_done = false;

/* Try to retrieve the first bonded peer's address.
 * Returns true and fills *out_addr if a bond is found, false otherwise. */
static bool get_bonded_peer(ble_addr_t *out_addr)
{
    struct ble_store_key_sec key = {0};
    struct ble_store_value_sec val = {0};
    key.peer_addr = *BLE_ADDR_ANY;   /* BLE_ADDR_ANY = "don't key off peer" → first bond */
    key.idx = 0;
    int rc = ble_store_read_our_sec(&key, &val);
    if (rc != 0) return false;                /* no bond */
    if (!val.ltk_present) return false;       /* not a full bond */
    *out_addr = val.peer_addr;
    return true;
}

static void start_advertising(void)
{
    /* Use random static address (set in ensure_random_static_addr via sync_cb) */
    uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;
    int rc;

    /* ── Phase 1: Low Duty Cycle Directed Advertising ──────────────────────
     * Directed advertising has NO payload (no adv fields, no scan response).
     * The peer is identified by address only.  disc_mode must be NON.
     * macOS BLE controller reconnects at the controller layer — no app-level
     * connect() call required in Claude Desktop.
     * After DIRECTED_ADV_DURATION_MS the ADV_COMPLETE event fires → Phase 2.
     * ────────────────────────────────────────────────────────────────────── */
    ble_addr_t bonded_peer;
    if (!s_directed_adv_done && get_bonded_peer(&bonded_peer)) {
        struct ble_gap_adv_params dir_params = {0};
        dir_params.conn_mode       = BLE_GAP_CONN_MODE_DIR;
        dir_params.disc_mode       = BLE_GAP_DISC_MODE_NON;  /* required for directed */
        dir_params.high_duty_cycle = 0;                       /* LDC: no 1.28 s limit */

        rc = ble_gap_adv_start(own_addr_type, &bonded_peer, DIRECTED_ADV_DURATION_MS,
                               &dir_params, ble_gap_event_cb, NULL);
        if (rc == 0) {
            ESP_LOGI(TAG, "directed adv → %02X:%02X:%02X:%02X:%02X:%02X (%d ms)",
                     bonded_peer.val[5], bonded_peer.val[4], bonded_peer.val[3],
                     bonded_peer.val[2], bonded_peer.val[1], bonded_peer.val[0],
                     DIRECTED_ADV_DURATION_MS);
            return;
        }
        ESP_LOGW(TAG, "directed adv failed rc=%d, using undirected", rc);
        s_directed_adv_done = true;  /* don't retry directed if it failed */
    }

    /* ── Phase 2: Undirected Advertising ───────────────────────────────────
     * Standard connectable undirected advertising with NUS UUID + name.
     * Required for new peers to discover and pair.
     * ────────────────────────────────────────────────────────────────────── */
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Adv packet: flags + NUS UUID + shortened name "Claude".
     * The shortened name ensures macOS passive-scan sees "claude…" even
     * before the scan response, satisfying Claude Desktop's
     * c.startsWith("claude") filter. */
    fields.uuids128 = (ble_uuid128_t *)&nus_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    fields.name = (const uint8_t *)"Claude";
    fields.name_len = 6;
    fields.name_is_complete = 0;   /* AD type 0x08 = Shortened Local Name */

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields failed: rc=%d", rc);
        return;
    }

    /* Scan response: full unique name "ClaudeXXYY". */
    const char *name = ble_svc_gap_device_name();
    if (name && name[0]) {
        struct ble_hs_adv_fields rsp;
        memset(&rsp, 0, sizeof(rsp));
        size_t name_len = strnlen(name, BLE_SCAN_RSP_MAX_NAME_LEN);
        rsp.name = (const uint8_t *)name;
        rsp.name_len = name_len;
        rsp.name_is_complete = 1;
        rc = ble_gap_adv_rsp_set_fields(&rsp);
        if (rc != 0) {
            ESP_LOGE(TAG, "adv rsp set fields: %d", rc);
            return;
        }
        ESP_LOGI(TAG, "device name: '%s' (len=%zu)", name, name_len);
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode  = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode  = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min   = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max   = BLE_GAP_ADV_ITVL_MS(100);

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
    if (rc == BLE_HS_EALREADY) return;
    if (rc != 0) { ESP_LOGE(TAG, "adv start: %d", rc); return; }
    ESP_LOGI(TAG, "undirected adv started");
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
            s_directed_adv_done = false;   /* reset so next disconnect gets a directed phase */
            s_ctx->secure = false;
            s_ctx->cccd_subscribed = false;
            s_passkey = 0;
            ESP_LOGI(TAG, "connected conn_handle=%d", s_ctx->conn_handle);
            /* Do NOT fire state_cb(CONNECTED) yet — wait for CCCD subscription
             * so the first TX (heartbeat ack) succeeds immediately. */
            /* Initiate security immediately so Desktop doesn't deadlock waiting
             * for us to start the pairing flow. ENC_CHANGE failure is handled
             * below: stale LTK is cleared and fresh pairing is retried. */
            ble_gap_security_initiate(event->connect.conn_handle);
        } else {
            /* Connection failed — peer likely has a stale LTK (e.g. after
             * reflash wiped NVS). Clear all stored bonds so the next attempt
             * forces fresh Just-Works pairing instead of looping on auth failures.
             * NOTE: BLE_GAP_EVENT_CONNECT failure does NOT produce a subsequent
             * DISCONNECT event, so we must restart advertising here ourselves. */
            ESP_LOGW(TAG, "connect failed rc=%d — clearing all bonds, restarting adv",
                     event->connect.status);
            ble_store_clear();
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        s_ctx->state = TRANSPORT_STATE_DISCONNECTED;
        s_ctx->secure = false;
        s_ctx->cccd_subscribed = false;
        s_passkey = 0;
        s_directed_adv_done = false;   /* allow directed phase for next reconnect */
        /* Use deferred callback (same as CONNECT) to keep NimBLE host task
         * stack clear of agent_core → state_machine call chains. */
        fire_state_cb_async(TRANSPORT_ID_BLE, TRANSPORT_STATE_DISCONNECTED,
                            s_ctx->base.cb_ctx);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* If directed adv timed out (not yet done), mark done and fall back
         * to undirected so that new peers can also discover and pair. */
        if (!s_directed_adv_done) {
            ESP_LOGI(TAG, "directed adv timed out — switching to undirected");
            s_directed_adv_done = true;
        } else {
            ESP_LOGI(TAG, "adv complete, restarting");
        }
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            s_ctx->secure = true;
            s_passkey = 0;
            ESP_LOGI(TAG, "encryption OK");
            /* Only fire CONNECTED if CCCD is already subscribed; otherwise
             * BLE_GAP_EVENT_SUBSCRIBE will fire it when the time is right. */
            if (s_ctx->cccd_subscribed)
                fire_state_cb_async(TRANSPORT_ID_BLE, TRANSPORT_STATE_CONNECTED,
                                    s_ctx->base.cb_ctx);
        } else {
            ESP_LOGW(TAG, "encryption FAIL status=%d — clearing stale bond, re-pairing",
                     event->enc_change.status);
            /* Stale LTK on desktop side: delete our stored bond and re-pair
             * with Just-Works so the link stays up this session. */
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            ble_gap_security_initiate(event->enc_change.conn_handle);
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
            s_passkey = (esp_random() % 1000000);
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
        /* Accept subscription on any handle to avoid +1 assumption failures.
         * NimBLE will only deliver notifies when CCCD is actually set.
         * Also fire the deferred CONNECTED callback here, so that the upper
         * layer's first ack TX is guaranteed to succeed.
         * Use async callback to avoid stack overflow in nimble_host task. */
        if (event->subscribe.cur_notify) {
            s_ctx->cccd_subscribed = true;
            ESP_LOGI(TAG, "TX notify on (handle=%d)", event->subscribe.attr_handle);
            fire_state_cb_async(TRANSPORT_ID_BLE, TRANSPORT_STATE_CONNECTED,
                                s_ctx->base.cb_ctx);
        } else if (s_ctx->cccd_subscribed) {
            s_ctx->cccd_subscribed = false;
            ESP_LOGI(TAG, "TX notify off");
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

    ESP_LOGD(TAG, "GATT access: op=%d attr=%d (rx_handle=%d)",
             ctxt->op, attr_handle, s_ctx->rx_val_handle);

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR &&
        attr_handle == s_ctx->rx_val_handle) {
        uint16_t pkt_len = OS_MBUF_PKTLEN(ctxt->om);
        ESP_LOGD(TAG, "RX %u bytes", pkt_len);

        /* Copy directly into rx_buf (which is heap-allocated) to avoid putting
         * a large local array on the nimble_host task stack (stack size ~5 KB). */
        uint16_t space = (uint16_t)(sizeof(s_ctx->rx_buf) - 1) - s_ctx->rx_pos;
        if (pkt_len > space) {
            ESP_LOGW(TAG, "RX overflow: reset buf (pos=%u pkt=%u)", s_ctx->rx_pos, pkt_len);
            s_ctx->rx_pos = 0;
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        int rc = os_mbuf_copydata(ctxt->om, 0, pkt_len, s_ctx->rx_buf + s_ctx->rx_pos);
        if (rc != 0) {
            ESP_LOGW(TAG, "RX copy failed: %d", rc);
            return BLE_ATT_ERR_UNLIKELY;
        }

        uint16_t end = s_ctx->rx_pos + pkt_len;
        for (uint16_t i = s_ctx->rx_pos; i < end; i++) {
            if (s_ctx->rx_buf[i] == '\n') {
                s_ctx->rx_buf[i] = '\0';
                ESP_LOGD(TAG, "RX line: %.*s", (int)i, s_ctx->rx_buf);
                if (s_ctx->base.rx_cb)
                    s_ctx->base.rx_cb(TRANSPORT_ID_BLE,
                                      (uint8_t *)s_ctx->rx_buf,
                                      i,
                                      s_ctx->base.cb_ctx);
                uint16_t remaining = end - i - 1;
                if (remaining > 0)
                    memmove(s_ctx->rx_buf, s_ctx->rx_buf + i + 1, remaining);
                end = remaining;
                i = (uint16_t)-1; /* restart scan; loop will i++ to 0 */
            }
        }
        s_ctx->rx_pos = end;
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

/* ── Persistent random static BLE address ────────────────────────────────
 * Stored in NVS so it survives normal reflash (app-only flash preserves NVS).
 * When NVS is erased (e.g. idf.py erase-flash), a new address is generated.
 * macOS sees a different BLE address → treats it as a new device → no stale
 * LTK conflict, fresh pairing happens automatically without "forget device".
 * ──────────────────────────────────────────────────────────────────────── */
#define BLE_ADDR_NVS_NS   "ble_addr"
#define BLE_ADDR_NVS_KEY  "rand_addr"

static void ensure_random_static_addr(void)
{
    uint8_t addr[6] = {0};
    nvs_handle_t h;
    esp_err_t err = nvs_open(BLE_ADDR_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed (%s), using chip MAC", esp_err_to_name(err));
        ble_hs_util_ensure_addr(0);
        return;
    }

    size_t len = sizeof(addr);
    err = nvs_get_blob(h, BLE_ADDR_NVS_KEY, addr, &len);
    if (err == ESP_OK && len == 6) {
        /* Address found in NVS — use it */
        ESP_LOGI(TAG, "BLE addr from NVS: %02X:%02X:%02X:%02X:%02X:%02X",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    } else {
        /* NVS was erased or first boot — generate a new random static address.
         * BLE spec: top 2 bits of byte[5] (MSB) must be 11 for static random. */
        esp_fill_random(addr, sizeof(addr));
        addr[5] |= 0xC0;   /* set top 2 bits → static random */
        err = nvs_set_blob(h, BLE_ADDR_NVS_KEY, addr, sizeof(addr));
        if (err == ESP_OK) nvs_commit(h);
        ESP_LOGI(TAG, "BLE addr generated: %02X:%02X:%02X:%02X:%02X:%02X",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }
    nvs_close(h);

    int rc = ble_hs_id_set_rnd(addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_set_rnd: %d — falling back to public addr", rc);
        ble_hs_util_ensure_addr(0);
    }
}

static void ble_hs_sync_cb(void)
{
    ensure_random_static_addr();
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
    ESP_LOGI(TAG, "ble_tp_start: entry");
    
    /* Step 1: Initialize NimBLE host */
    nimble_port_init();
    ESP_LOGI(TAG, "nimble_port_init OK");

    /* Step 2: Configure host callbacks */
    ble_hs_cfg.sync_cb = ble_hs_sync_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Security manager: keep bonding enabled, but do not require MITM
     * passkey entry by default to avoid desktop pairing UI deadlocks.
     * macOS / Claude Desktop does not support passkey entry; use Just-Works. */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ESP_LOGI(TAG, "BLE SM config OK");

    /* Step 3: Initialize GAP/GATT services, register custom services */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_LOGI(TAG, "GAP/GATT services init OK");

    int rc = ble_gatts_count_cfg(nus_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_count_cfg: %d", rc); return ESP_FAIL; }

    rc = ble_gatts_add_svcs(nus_gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_add_svcs: %d", rc); return ESP_FAIL; }
    ESP_LOGI(TAG, "NUS service registered");

    /* Step 4: Set device name AFTER GATT services registered */
    ble_svc_gap_device_name_set(s_ctx->device_name);
    ESP_LOGI(TAG, "Device name set to: %s", s_ctx->device_name);

    /* Step 5: Initialize NVS storage for bonding */
    ble_store_config_init();
    ESP_LOGI(TAG, "BLE store config OK");

    /* Note: val handles are synced in ble_hs_sync_cb() after NimBLE assigns them. */

    /* Step 6: Create deferred state callback timer */
    esp_timer_create_args_t sta = {
        .callback = deferred_state_cb,
        .name     = "ble_state",
    };
    ESP_ERROR_CHECK(esp_timer_create(&sta, &s_state_timer));
    ESP_LOGI(TAG, "State timer created");

    /* Step 7: Start host task (triggers sync_cb → advertising) */
    ESP_LOGI(TAG, "Starting NimBLE host task...");
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "nimble_port_freertos_init returned");

    return ESP_OK;
}

static esp_err_t ble_tp_stop(transport_t *t)
{
    if (s_state_timer) {
        esp_timer_stop(s_state_timer);
        esp_timer_delete(s_state_timer);
        s_state_timer = NULL;
    }
    nimble_port_stop();
    return ESP_OK;
}

static esp_err_t ble_tp_send(transport_t *t, const uint8_t *data, size_t len)
{
    ble_ctx_t *ctx = (ble_ctx_t *)t;
    if (ctx->state != TRANSPORT_STATE_CONNECTED) return ESP_ERR_INVALID_STATE;
    if (ctx->tx_val_handle == 0) return ESP_ERR_INVALID_STATE;

    size_t chunk = (ctx->mtu > 3) ? ctx->mtu - 3 : 20;
    if (chunk > 180) chunk = 180;
    size_t offset = 0;
    while (offset < len) {
        size_t n = (len - offset < chunk) ? (len - offset) : chunk;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + offset, n);
        /* Use ble_gatts_notify_custom (server API) which validates CCCD
         * internally. ble_gattc_notify_custom is deprecated and skips
         * the CCCD check, causing unexpected behaviour. */
        int rc = ble_gatts_notify_custom(ctx->conn_handle, ctx->tx_val_handle, om);
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
    } else if (CONFIG_TRANSPORT_BLE_DEVICE_NAME[0]) {
        strlcpy(ctx->device_name, CONFIG_TRANSPORT_BLE_DEVICE_NAME, sizeof(ctx->device_name));
    } else {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        /* Claude Desktop filters picker results by "Claude" prefix. */
        snprintf(ctx->device_name, sizeof(ctx->device_name),
                 "Claude-%02X%02X", mac[4], mac[5]);
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
