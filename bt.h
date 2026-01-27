#ifndef ZTL_BT_H_
#define ZTL_BT_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

int ztl_bt__init(char const* device_name, uint8_t const* manufacture_data, size_t const manufacture_data_size);
int ztl_bt__connection(struct bt_conn** connection);

#endif // ZTL_BT_H_
