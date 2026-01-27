#include "common_workqueue.h"

#include <safe-c/safe_c.h>

LOG_MODULE_REGISTER(ztl);

int ztl__init(void) {
    TRY(_ztl_common_workqueue__init());
    return 0;
}