#ifndef ZTL_BT_H_
#define ZTL_BT_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/zbus/zbus.h>

#define ZTL_BT_UUID_128_ENCODE(manufacture_part, device_part, service_part, charc_part) \
	BT_BYTES_LIST_LE64((uint64_t)(manufacture_part)),\
	BT_BYTES_LIST_LE48((uint64_t)(device_part)), \
	service_part, \
	charc_part

#define ZTL_BT_UUID_SERVICE(service_part_w8) BT_UUID_INIT_128(ZTL_BT_UUID_128_ENCODE( \
    CONFIG_ZTL_BT_UUID_MANUFACTURE_PART, CONFIG_ZTL_BT_UUID_DEVICE_PART, service_part_w8, 0))

#define ZTL_BT_UUID_CHARC(service_part_w8, charc_part_w8) BT_UUID_INIT_128(ZTL_BT_UUID_128_ENCODE( \
    CONFIG_ZTL_BT_UUID_MANUFACTURE_PART, CONFIG_ZTL_BT_UUID_DEVICE_PART, service_part_w8, charc_part_w8))

enum ZtlBtEventType {
    ZTL_BT_EVENT__CONNECTED,
    ZTL_BT_EVENT__DISCONNECTED,
};

struct ZtlBtEvent {
    enum ZtlBtEventType type;
    struct bt_conn* conn;
};

ZBUS_CHAN_DECLARE(e_ztl_bt_chan);

int ztl_bt__init(char const* device_name, uint8_t const* manufacture_data, size_t const manufacture_data_size);

#endif // ZTL_BT_H_
