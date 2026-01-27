#ifndef ZTL_NVS_H_
#define ZTL_NVS_H_

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>

int ztl_nvs__init_manual(struct nvs_fs* fs, struct device const* flash_device, off_t offset, uint32_t size);

#define ztl_nvs__init(fs, partition) ztl_nvs__init_manual(fs, \
    FIXED_PARTITION_DEVICE(partition), FIXED_PARTITION_OFFSET(partition), FIXED_PARTITION_SIZE(partition))

#endif // ZTL_NVS_H_
