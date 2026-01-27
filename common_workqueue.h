#ifndef ZTL_COMMON_WORKQUEUE_H_
#define ZTL_COMMON_WORKQUEUE_H_

#include <zephyr/kernel.h>

int ztl_common_workqueue__submit(struct k_work* work);
int ztl_common_workqueue__schedule(struct k_work_delayable* work, k_timeout_t delay);
int ztl_common_workqueue__reschedule(struct k_work_delayable* dwork, k_timeout_t delay);

int _ztl_common_workqueue__init(void);

#endif // ZTL_COMMON_WORKQUEUE_H_
