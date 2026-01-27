#include "sensor.h"

#include <safe_c.h>

LOG_MODULE_DECLARE(ztl);

int ztl_sensor__get_double(struct device const* dev, enum sensor_channel ch, double* val) {
    struct sensor_value val_raw = {0};

    TRY(sensor_sample_fetch(dev));
    TRY(sensor_channel_get(dev, ch, &val_raw));
    *val = sensor_value_to_double(&val_raw);

    return 0;
}
