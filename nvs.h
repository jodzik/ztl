#ifndef ZTL_NVS_H_
#define ZTL_NVS_H_

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>

enum {
    ZTL_NVS_RING_MAX_EXTENT = UINT16_MAX - 1,
};

typedef struct ZtlNvsRing {
    struct nvs_fs fs;
    uint16_t extent;
    uint16_t current_extent;
    uint16_t last_nvs_id;
} ZtlNvsRing;

int ztl_nvs__init_manual(struct nvs_fs* fs, struct device const* flash_device, off_t offset, uint32_t size);

#define ztl_nvs__init(fs, partition) ztl_nvs__init_manual(fs, \
    PARTITION_DEVICE(partition), PARTITION_OFFSET(partition), PARTITION_SIZE(partition))

int ztl_nvs_ring__init_manual(
    struct ZtlNvsRing* nvs_ring,
    uint16_t extent,
    struct device const* flash_device,
    off_t offset,
    uint32_t size);

#define ztl_nvs_ring__init(nvs_ring, extent, partition) ztl_nvs_ring__init_manual(nvs_ring, extent, \
    PARTITION_DEVICE(partition), PARTITION_OFFSET(partition), PARTITION_SIZE(partition))

/// @brief Get element from ring nvs buffer.
/// @param nvs_ring[in] - nvs ring buffer.
/// @param index[in] - index of element from end, 0 - last, newest element.
/// @param buf[out] - element buffer.
/// @param buf_size[in] - max size of element buf.
/// @param real_size[out] - real size of read element.
/// @return 0 is success, else if error.
int ztl_nvs_ring__at(
    struct ZtlNvsRing* nvs_ring,
    uint16_t index,
    uint8_t* buf,
    uint16_t buf_size,
    uint16_t* real_size);

int ztl_nvs_ring__append(struct ZtlNvsRing* nvs_ring, uint8_t const* data, uint16_t data_size);

int ztl_nvs_ring__extent(struct ZtlNvsRing const* nvs_ring, uint16_t* extent);

int ztl_nvs_ring__current_extent(struct ZtlNvsRing const* nvs_ring, uint16_t* current_extent);

#endif // ZTL_NVS_H_
