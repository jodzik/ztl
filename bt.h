#ifndef ZTL_BT_H_
#define ZTL_BT_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/zbus/zbus.h>

enum ZtlBtEventType {
    ZTL_BT_EVENT__CONNECTED,
    ZTL_BT_EVENT__DISCONNECTED,
};

struct ZtlBtEvent {
    enum ZtlBtEventType type;
    struct bt_conn* conn;
};

ZBUS_CHAN_DECLARE(ztl_bt_chan);

int ztl_bt__init(char const* device_name, uint8_t const* manufacture_data, size_t const manufacture_data_size);

#endif // ZTL_BT_H_
