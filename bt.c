#include "bt.h"

#include <safe-c/safe_c.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>
#include <zephyr/zbus/zbus.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

LOG_MODULE_REGISTER(ztl_bt);

ZBUS_CHAN_DEFINE(e_ztl_bt_chan, struct ZtlBtEvent, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT());

enum BtStatus {
    BT_STATUS__NOT_INIT = 0,
    BT_STATUS__INITIALIZE = 1,
    BT_STATUS__OK = 2,
    BT_STATUS__FAIL = 3,
};

enum {
    /* Ask the central to encrypt the link only if it has not done so by
     * itself. BlueZ answers an early security request with Pairing Failed
     * 0x5 when it holds no bond (host tools remove bonds before connecting)
     * and flags the peer as failed-authentication - that state later makes
     * bluetoothd drop the whole connection after re-pairing. Centrals that
     * pair by themselves (host tools via explicit pair(), Android) finish
     * within about a second, so wait before requesting. */
    SECURITY_REQUEST_DELAY_MS = 1500,
    /* Advertising restart backoff after a failed bt_le_adv_start(): doubles
     * up to ADV_RETRY_DELAY_MAX_MS until advertising is running. */
    ADV_RETRY_DELAY_MIN_MS = 1000,
    ADV_RETRY_DELAY_MAX_MS = 30000,
    /* Supervision period in disconnected mode: re-assert advertising in case
     * every faster path (recycled callback, retry work) missed it, e.g. a
     * leaked connection reference keeps recycled() from ever firing. A start
     * attempt on a running advertiser returns -EALREADY before any HCI
     * traffic (adv.c checks BT_ADV_ENABLED first), so the check is cheap. */
    ADV_SUPERVISION_PERIOD_MS = 60000,
};

static char g_device_name_buf[32] = {0};
static uint8_t g_manufacture_data_buf[32] = {0};
/* BT_DATA_BYTES() builds the AD element from a stack compound literal that
 * dies when ztl_bt__init() returns; every later bt_le_adv_start() (recycled
 * callback, supervision) re-reads the dangling pointer and advertises garbage
 * flags. Observed on device: 0x00-0x03 instead of 0x06 - broken
 * discoverability, and 0x01 (Limited Discoverable) silently arms the host's
 * 30 s limited-advertising timeout which stops advertising (see adv.c
 * ad_is_limited() + CONFIG_BT_LIM_ADV_TIMEOUT). */
static uint8_t const g_ad_flags[1] = { (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR) };
static struct bt_data g_advertise_data[3] = {0};
static uint8_t g_advertise_data_count = 0;
static struct bt_data g_scan_response_data[2] = {0};
static uint8_t g_scan_response_count = 0;

/// `enum BtStatus`
static atomic_t g_bt_status = ATOMIC_INIT(BT_STATUS__NOT_INIT);
/// Connected peers count, 0 means disconnected mode (supervision may advertise).
static atomic_t g_conn_count = ATOMIC_INIT(0);
/// Current retry backoff for the advertising restart, `ADV_RETRY_DELAY_*_MS`.
static atomic_t g_adv_retry_delay_ms = ATOMIC_INIT(ADV_RETRY_DELAY_MIN_MS);

static void _security_request_work_handler(struct k_work* const work);
static void _adv_retry_work_handler(struct k_work* const work);
static void _adv_supervision_work_handler(struct k_work* const work);

static K_MUTEX_DEFINE(g_security_request_mutex);
static K_WORK_DELAYABLE_DEFINE(g_security_request_work, _security_request_work_handler);
/// Connection waiting for its delayed security request, NULL when none.
static struct bt_conn* g_security_request_conn = NULL;

static K_WORK_DELAYABLE_DEFINE(g_adv_retry_work, _adv_retry_work_handler);
static K_WORK_DELAYABLE_DEFINE(g_adv_supervision_work, _adv_supervision_work_handler);

/* Runs in the system workqueue SECURITY_REQUEST_DELAY_MS after connect: ask
 * the central to encrypt the link unless it already did. */
static void _security_request_work_handler(struct k_work* const work) {
    struct bt_conn* conn = NULL;

    k_mutex_lock(&g_security_request_mutex, K_FOREVER);
    conn = g_security_request_conn;
    g_security_request_conn = NULL;
    k_mutex_unlock(&g_security_request_mutex);

    if (NULL == conn) {
        return;
    }

    if (bt_conn_get_security(conn) < BT_SECURITY_L2) {
        int const rc = bt_conn_set_security(conn, BT_SECURITY_L2);
        if (rc) {
            LOG_ERR("Security request failed (err %d)", rc);
        }
    }

    bt_conn_unref(conn);
}

/* See SECURITY_REQUEST_DELAY_MS. Holds a connection reference until the
 * request work fires (or the link drops). */
static void _schedule_security_request(struct bt_conn* const conn) {
    struct bt_conn* stale = NULL;

    k_mutex_lock(&g_security_request_mutex, K_FOREVER);
    stale = g_security_request_conn;
    g_security_request_conn = bt_conn_ref(conn);
    k_work_reschedule(&g_security_request_work, K_MSEC(SECURITY_REQUEST_DELAY_MS));
    k_mutex_unlock(&g_security_request_mutex);

    if (NULL != stale) {
        bt_conn_unref(stale);
    }
}

/* Idempotent advertising start. Returns 0 on success and -EALREADY when
 * advertising is already running (both mean the device is connectable). */
static int _adv_start(void) {
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, g_advertise_data, g_advertise_data_count,
        g_scan_response_data, g_scan_response_count);
}

static void _adv_running(void) {
    atomic_set(&g_bt_status, BT_STATUS__OK);
    LOG_INF("Mode: waiting connections...");
}

/* Advertising restart with exponential backoff, armed by recycled() after a
 * failed start attempt (e.g. -ENOMEM while the connection object is still
 * referenced by an in-flight request). Retries until advertising runs. */
static void _adv_retry_work_handler(struct k_work* const work) {
    int const rc = _adv_start();
    int delay_ms = 0;

    UNUSED(work);

    if (0 == rc) {
        LOG_INF("Advertising started by retry");
        _adv_running();
        return;
    }

    if (-EALREADY == rc) {
        _adv_running();
        return;
    }

    delay_ms = atomic_get(&g_adv_retry_delay_ms);
    if (ADV_RETRY_DELAY_MAX_MS > delay_ms) {
        delay_ms = MIN(ADV_RETRY_DELAY_MAX_MS, delay_ms * 2);
        atomic_set(&g_adv_retry_delay_ms, delay_ms);
    }

    LOG_WRN("Advertising retry failed (err %d), next attempt in %d ms", rc, delay_ms);
    k_work_reschedule(&g_adv_retry_work, K_MSEC(delay_ms));
}

/* Last-resort supervision: while in disconnected mode keep re-asserting
 * advertising so a single missed callback can not silence the device. */
static void _adv_supervision_work_handler(struct k_work* const work) {
    int rc = 0;

    UNUSED(work);

    if (0 == atomic_get(&g_conn_count)) {
        rc = _adv_start();
        if (0 == rc) {
            LOG_WRN("Advertising restarted by supervision");
            _adv_running();
        } else if (-EALREADY != rc) {
            LOG_WRN("Supervision: advertising not running (err %d)", rc);
        }
    }

    k_work_reschedule(&g_adv_supervision_work, K_MSEC(ADV_SUPERVISION_PERIOD_MS));
}

static void connected(struct bt_conn* const conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        atomic_inc(&g_conn_count);
        LOG_INF("Connected");
        _schedule_security_request(conn);
        struct ZtlBtEvent event = {
            .type = ZTL_BT_EVENT__CONNECTED,
            .conn = bt_conn_ref(conn),
        };
        zbus_chan_pub(&e_ztl_bt_chan, &event, K_FOREVER);
    }
}

static void disconnected(struct bt_conn* const conn, uint8_t reason) {
    struct bt_conn* pending = NULL;

    LOG_INF("Disconnected, reason %u %s", reason, bt_hci_err_to_str(reason));

    atomic_dec(&g_conn_count);

    k_mutex_lock(&g_security_request_mutex, K_FOREVER);
    if (conn == g_security_request_conn) {
        pending = g_security_request_conn;
        g_security_request_conn = NULL;
    }
    k_mutex_unlock(&g_security_request_mutex);
    if (NULL != pending) {
        bt_conn_unref(pending);
    }

    struct ZtlBtEvent event = {
        .type = ZTL_BT_EVENT__DISCONNECTED,
        .conn = conn,
    };
    zbus_chan_pub(&e_ztl_bt_chan, &event, K_FOREVER);
    /* No bt_conn_unref() here: the callback does not own a reference to
     * conn (the stack keeps one until the connection object is recycled);
     * the zbus CONNECTED event reference is released by the auth listener. */
}

/* Fires on every connection object return to the pool, not once per
 * disconnect: with CONFIG_BT_MAX_CONN == 1 each bt_le_adv_start() reserves
 * the freed object for the incoming connection (adv.c le_adv_start_add_conn),
 * so releases elsewhere (e.g. an advertiser stop racing this callback) fire
 * it again while advertising is already running. */
static void recycled(void) {
    int const rc = _adv_start();

    if (0 == rc) {
        _adv_running();
        return;
    }

    if (-EALREADY == rc) {
        LOG_INF("Advertising already running");
        return;
    }

    LOG_ERR("Advertising failed to start (err %d), retry in %d ms", rc, ADV_RETRY_DELAY_MIN_MS);
    atomic_set(&g_bt_status, BT_STATUS__FAIL);
    atomic_set(&g_adv_retry_delay_ms, ADV_RETRY_DELAY_MIN_MS);
    k_work_reschedule(&g_adv_retry_work, K_MSEC(ADV_RETRY_DELAY_MIN_MS));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = recycled,
};

static void bt_ready(int err)
{
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        atomic_set(&g_bt_status, BT_STATUS__FAIL);
        return;
    }
    LOG_INF("Bluetooth initialized");

    atomic_set(&g_bt_status, BT_STATUS__OK);
}

int ztl_bt__init(char const* device_name, struct bt_uuid_128 const* service_uuid,
    uint8_t const* manufacture_data, size_t const manufacture_data_size) {
    enum {
        MAX_LEGACY_ADVERTISE_DATA_SIZE = 31,
    };

    int rc = 0;
    size_t const device_name_len = strlen(device_name);

    ASSERT(device_name_len < sizeof(g_device_name_buf), ER_INVAL);
    ASSERT(manufacture_data_size <= sizeof(g_manufacture_data_buf), ER_INVAL);

    strlcpy(g_device_name_buf, device_name, sizeof(g_device_name_buf));

    struct bt_data const flags = BT_DATA(BT_DATA_FLAGS, g_ad_flags, sizeof(g_ad_flags));
    struct bt_data const name = BT_DATA(BT_DATA_NAME_COMPLETE, g_device_name_buf, device_name_len);

    if (NULL != service_uuid) {
        size_t const scan_response_size = 2 + device_name_len
            + ((NULL != manufacture_data) ? 2 + manufacture_data_size : 0);

        ASSERTf(scan_response_size <= MAX_LEGACY_ADVERTISE_DATA_SIZE, ER_INVAL,
            "scan_response_size(%zu) is greater than max legacy advertise payload size(%i)",
            scan_response_size, MAX_LEGACY_ADVERTISE_DATA_SIZE);

        g_advertise_data[0] = flags;
        struct bt_data const uuid = BT_DATA(BT_DATA_UUID128_ALL, service_uuid->val, BT_UUID_SIZE_128);
        g_advertise_data[1] = uuid;
        g_advertise_data_count = 2;

        g_scan_response_data[0] = name;
        g_scan_response_count = 1;

        if (NULL != manufacture_data) {
            memcpy(g_manufacture_data_buf, manufacture_data, manufacture_data_size);

            struct bt_data const manuf = BT_DATA(BT_DATA_MANUFACTURER_DATA, g_manufacture_data_buf, manufacture_data_size);
            g_scan_response_data[1] = manuf;
            g_scan_response_count = 2;
        }
    } else {
        ASSERTf(device_name_len + manufacture_data_size <= MAX_LEGACY_ADVERTISE_DATA_SIZE, ER_INVAL,
            "device_name_len(%zu) + manufacture_data_size(%zu) is greater than max legacy advertise payload size(%i)",
            device_name_len, manufacture_data_size, MAX_LEGACY_ADVERTISE_DATA_SIZE);

        g_advertise_data[0] = flags;
        g_advertise_data[1] = name;
        g_advertise_data_count = 2;
        g_scan_response_count = 0;

        if (NULL != manufacture_data) {
            memcpy(g_manufacture_data_buf, manufacture_data, manufacture_data_size);

            struct bt_data const manuf = BT_DATA(BT_DATA_MANUFACTURER_DATA, g_manufacture_data_buf, manufacture_data_size);
            g_advertise_data[2] = manuf;
            g_advertise_data_count = 3;
        }
    }

    atomic_set(&g_bt_status, BT_STATUS__INITIALIZE);
    TRY(bt_enable(bt_ready));

    while (BT_STATUS__INITIALIZE == atomic_get(&g_bt_status)) {
        k_msleep(1);
    }

    ASSERT(BT_STATUS__OK == atomic_get(&g_bt_status), ER_NO_DEV);

    /* Load settings after bt_enable(): the "bt" tree creates or restores the
     * identity address and finalizes BT init, so advertising can start. */
    TRY(settings_load());

    rc = _adv_start();
    ASSERTf(0 == rc, rc, "Advertising failed to start (err %d)", rc);

    _adv_running();
    k_work_reschedule(&g_adv_supervision_work, K_MSEC(ADV_SUPERVISION_PERIOD_MS));

 finally:

    if (0 != rc) {
        atomic_set(&g_bt_status, BT_STATUS__FAIL);
    }

    return rc;
}
