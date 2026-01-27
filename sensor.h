#ifndef ZTL_SENSOR_H_
#define ZTL_SENSOR_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

int ztl_sensor__get_double(struct device const* dev, enum sensor_channel ch, double* val);

#endif // ZTL_SENSOR_H_
