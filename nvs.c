#include "nvs.h"

#include <safe-c/safe_c.h>

LOG_MODULE_REGISTER(ztl_nvs);

int ztl_nvs__init_manual(struct nvs_fs* fs, struct device const* flash_device, off_t offset, uint32_t size) {
    struct flash_pages_info info = {0};

    ASSERT(device_is_ready(flash_device), ER_NO_DEV);

    fs->flash_device = flash_device;
    fs->offset = offset;
    TRY(flash_get_page_info_by_offs(flash_device, offset, &info));
    fs->sector_size = info.size;
    fs->sector_count = size / fs->sector_size;

    TRY(nvs_mount(fs));

    LOG_INF("FS was mounted: offset=%li size=%u sector_size=%u sector_count=%u",
        offset, size, fs->sector_size, fs->sector_count);

    return 0;
}
