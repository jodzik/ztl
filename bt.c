#include "bt.h"

#include <safe-c/safe_c.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>
#include <zephyr/zbus/zbus.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

LOG_MODULE_REGISTER(ztl_bt);

ZBUS_CHAN_DEFINE(e_ztl_bt_chan, struct ZtlBtEvent, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT());

enum BtStatus {
    BT_STATUS__NOT_INIT = 0,
    BT_STATUS__INITIALIZE = 1,
    BT_STATUS__OK = 2,
    BT_STATUS__FAIL = 3,
};

static char g_device_name_buf[32] = {0};
static uint8_t g_manufacture_data_buf[32] = {0};
static struct bt_data g_advertise_data[3] = {0};
static uint8_t g_advertise_data_count = 0;

/// `enum BtStatus`
static atomic_t g_bt_status = ATOMIC_INIT(BT_STATUS__NOT_INIT);

static void connected(struct bt_conn* const conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected");
        struct ZtlBtEvent event = {
            .type = ZTL_BT_EVENT__CONNECTED,
            .conn = bt_conn_ref(conn),
        };
        zbus_chan_pub(&e_ztl_bt_chan, &event, K_FOREVER);
    }
}

static void disconnected(struct bt_conn* const conn, uint8_t reason) {
    LOG_INF("Disconnected, reason %u %s", reason, bt_hci_err_to_str(reason));
    struct ZtlBtEvent event = {
        .type = ZTL_BT_EVENT__DISCONNECTED,
        .conn = conn,
    };
    zbus_chan_pub(&e_ztl_bt_chan, &event, K_FOREVER);
    bt_conn_unref(conn);
}

static void recycled(void) {
    int rc = 0;
    rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, g_advertise_data, g_advertise_data_count, NULL, 0);
    if (rc) {
        LOG_ERR("Advertising failed to start (err %d)", rc);
        atomic_set(&g_bt_status, BT_STATUS__FAIL);
    } else {
        atomic_set(&g_bt_status, BT_STATUS__OK);
        LOG_INF("Mode: waiting connections...");
    }
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

int ztl_bt__init(char const* device_name, uint8_t const* manufacture_data, size_t const manufacture_data_size) {
    enum {
        MAX_LEGACY_ADVERTISE_DATA_SIZE = 31,
    };

    int rc = 0;
    size_t const device_name_len = strlen(device_name);

    ASSERT(device_name_len < sizeof(g_device_name_buf), ER_INVAL);
    ASSERT(manufacture_data_size <= sizeof(g_manufacture_data_buf), ER_INVAL);
    ASSERTf(device_name_len + manufacture_data_size <= MAX_LEGACY_ADVERTISE_DATA_SIZE, ER_INVAL,
        "device_name_len(%zu) + manufacture_data_size(%zu) is greater than max legacy advertise payload size(%i)",
        device_name_len, manufacture_data_size, MAX_LEGACY_ADVERTISE_DATA_SIZE);

    {
        strlcpy(g_device_name_buf, device_name, sizeof(g_device_name_buf));

        struct bt_data const flags = BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));
        g_advertise_data[0] = flags;

        struct bt_data const name = BT_DATA(BT_DATA_NAME_COMPLETE, g_device_name_buf, device_name_len);
        g_advertise_data[1] = name;

        g_advertise_data_count = 2;
    }

    if (NULL != manufacture_data) {
        memcpy(g_manufacture_data_buf, manufacture_data, manufacture_data_size);

        struct bt_data const manuf = BT_DATA(BT_DATA_MANUFACTURER_DATA, g_manufacture_data_buf, manufacture_data_size);
        g_advertise_data[2] = manuf;
        g_advertise_data_count = 3;
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

    rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, g_advertise_data, g_advertise_data_count, NULL, 0);
    ASSERTf(0 == rc, rc, "Advertising failed to start (err %d)", rc);

    LOG_INF("Mode: waiting connections...");

 finally:

    if (0 != rc) {
        atomic_set(&g_bt_status, BT_STATUS__FAIL);
    }

    return rc;
}
