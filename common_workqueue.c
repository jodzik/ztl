#include "common_workqueue.h"

#include <safe-c/safe_c.h>

#include <zephyr/autoconf.h>

K_THREAD_STACK_DEFINE(g_common_workqueue_stack, CONFIG_ZTL_COMMON_WORKQUEUE_STACK_SIZE);
static struct k_work_q g_common_workqueue = {0};

int ztl_common_workqueue__submit(struct k_work* work) {
    return k_work_submit_to_queue(&g_common_workqueue, work);
}

int ztl_common_workqueue__schedule(struct k_work_delayable* dwork, k_timeout_t delay) {
    return k_work_schedule_for_queue(&g_common_workqueue, dwork, delay);
}

int ztl_common_workqueue__reschedule(struct k_work_delayable* dwork, k_timeout_t delay) {
    return k_work_reschedule_for_queue(&g_common_workqueue, dwork, delay);
}

int _ztl_common_workqueue__init(void) {
    k_work_queue_init(&g_common_workqueue);

    struct k_work_queue_config cfg = {0};
    cfg.name = "ztl-cmn-wq";
    cfg.no_yield = false;
    cfg.essential = false;
    cfg.work_timeout_ms = CONFIG_ZTL_COMMON_WORKQUEUE_TIMEOUT_MS;
    k_work_queue_start(&g_common_workqueue, g_common_workqueue_stack, K_THREAD_STACK_SIZEOF(g_common_workqueue_stack),
        CONFIG_ZTL_COMMON_WORKQUEUE_PRIO, &cfg);

    return 0;
}
