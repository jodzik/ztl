#include "common_workqueue.h"

#include <safe-c/safe_c.h>

LOG_MODULE_REGISTER(ztl);

int ztl__init(void) {
    int rc = 0;
    TRY(_ztl_common_workqueue__init());
    LOG_INF("Init.");
 finally:
    return rc;
}