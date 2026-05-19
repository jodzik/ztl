#include "nvs.h"

#include <safe-c/safe_c.h>

LOG_MODULE_REGISTER(ztl_nvs);

enum {
    MAX_NVS_RING_ID = ZTL_NVS_RING_MAX_EXTENT,
    NVS_RING_SERVICE_ID = UINT16_MAX,
};

typedef struct __packed NvsRingServiceRecord {
    uint16_t current_extent;
    uint16_t last_nvs_id;
} NvsRingServiceRecord;

int ztl_nvs__init_manual(struct nvs_fs* fs, struct device const* flash_device, off_t offset, uint32_t size) {
    struct flash_pages_info page_info = {0};

    ASSERT(device_is_ready(flash_device), ER_NO_DEV);

    fs->flash_device = flash_device;
    fs->offset = offset;

    TRY(flash_get_page_info_by_offs(flash_device, 0, &page_info));
    ASSERT(0 != page_info.size, ER_BAD_FILE);

    fs->sector_size = page_info.size;
    fs->sector_count = size / fs->sector_size;
    ASSERT(fs->sector_count >= 2, ER_BAD_FILE);

    TRY(nvs_mount(fs));

    LOG_INF("FS was mounted: offset=%li size=%u sector_size=%u sector_count=%u",
        offset, size, fs->sector_size, fs->sector_count);

    return 0;
}

int ztl_nvs_ring__init_manual(
    struct ZtlNvsRing* const nvs_ring,
    uint16_t const extent,
    struct device const* const flash_device,
    off_t const offset,
    uint32_t const size)
{
    struct NvsRingServiceRecord service_rec = {0};
    memset(nvs_read, 0, sizeof(*nvs_ring));
    TRY(ztl_nvs__init_manual(&nvs_ring->fs, flash_device, offset, size));

    nvs_ring->extent = extent;
    ssize_t const check = nvs_read(&nvs_ring->fs, NVS_RING_SERVICE_ID, &service_rec, sizeof(service_rec));
    if (sizeof(service_rec) == check) {
        if (service_rec.current_extent > extent) {
            nvs_ring->current_extent = extent;
        } else {
            nvs_ring->current_extent = service_rec.current_extent;
        }
        nvs_ring->last_nvs_id = service_rec.last_nvs_id;
    }

    return 0;
}

inline static uint16_t nvs_id_by_index(struct ZtlNvsRing const* const nvs_ring, uint16_t const index) {
    if (index <= nvs_ring->last_nvs_id) {
        return nvs_ring->last_nvs_id - index;
    } else {
        return MAX_NVS_RING_ID - index + nvs_ring->last_nvs_id + 1;
    }
}

int ztl_nvs_ring__at(
    struct ZtlNvsRing* const nvs_ring,
    uint16_t const index,
    uint8_t* const buf,
    uint16_t const buf_size,
    uint16_t* const real_size)
{
    ASSERT(NULL != nvs_ring, ER_INVAL);
    ASSERT(NULL != buf, ER_INVAL);
    ASSERT(buf_size > 0, ER_INVAL);
    ASSERT(NULL != real_size, ER_INVAL);
    ASSERT(index < nvs_ring->current_extent, ER_NO_ENT);

    uint16_t const id = nvs_id_by_index(nvs_ring, index);
    ssize_t const check = nvs_read(&nvs_ring->fs, id, buf, buf_size);
    ASSERT(check > 0, (int)check);
    ASSERT(check < UINT16_MAX, ER_OVERFLOW);
    *real_size = (uint16_t)check;

    return 0;
}

int ztl_nvs_ring__append(struct ZtlNvsRing* const nvs_ring, uint8_t const* const data, uint16_t const data_size) {
    ASSERT(NULL != nvs_ring, ER_INVAL);
    ASSERT(NULL != data, ER_INVAL);

    if (nvs_ring->current_extent == nvs_ring->extent) {
        uint16_t const id_to_delete = nvs_id_by_index(nvs_ring, nvs_ring->current_extent - 1);
        TRY_PASS(nvs_delete(&nvs_ring->fs, id_to_delete));
    }

    if (MAX_NVS_RING_ID == nvs_ring->last_nvs_id) {
        nvs_ring->last_nvs_id = 0;
    } else {
        nvs_ring->last_nvs_id += 1;
    }

    TRY(nvs_write(&nvs_ring->fs, nvs_ring->last_nvs_id, data, data_size));

    return 0;
}

int ztl_nvs_ring__extent(struct ZtlNvsRing const* const nvs_ring, uint16_t* const extent) {
    ASSERT(NULL != nvs_ring, ER_INVAL);
    ASSERT(NULL != extent, ER_INVAL);

    *extent = nvs_ring->extent;

    return 0;
}

int ztl_nvs_ring__current_extent(struct ZtlNvsRing const* const nvs_ring, uint16_t* const current_extent) {
    ASSERT(NULL != nvs_ring, ER_INVAL);
    ASSERT(NULL != current_extent, ER_INVAL);

    *current_extent = nvs_ring->current_extent;

    return 0;
}
