#ifndef ZTL_BT_H_
#define ZTL_BT_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

enum ZtlBtEvents {
    ZTL_BT_EVENT__CONNECTED = CONFIG_ZTL_BT_EVENT_BASE + 1,  // bt_conn*
    ZTL_BT_EVENT__DISCONNECTED = CONFIG_ZTL_BT_EVENT_BASE + 2,   // bt_conn*
};

int ztl_bt__init(char const* device_name, uint8_t const* manufacture_data, size_t const manufacture_data_size);

#endif // ZTL_BT_H_
