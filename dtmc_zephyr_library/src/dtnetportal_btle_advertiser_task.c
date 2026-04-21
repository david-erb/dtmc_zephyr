#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtunittest.h>

#include <dtmc_base/dttasker.h>

#include <dtmc/dtmc_zephyr.h>

#include <dtmc/dtnetportal_btle.h>

#include "dtnetportal_btle_private.h"

#define TAG "dtnetportal_btle_advertiser_task"

// -------------------------------------------------------------------------------
// this executes the main logic of the task
dterr_t*
dtnetportal_btle_advertiser_task_entrypoint(void* self_arg, dttasker_handle tasker_handle)
{
    dtnetportal_btle_t* self = (dtnetportal_btle_t*)self_arg;
    dterr_t* dterr = NULL;
    bool first_time = true;

    dtlog_info(TAG, "business logic started on self %p", self);

    while (true)
    {
        dtlog_info(TAG, "starting advertising on self %p", self);
        DTERR_C(dtnetportal_btle_start_advertising(self));

        if (first_time)
        {
            DTERR_C(dttasker_ready(tasker_handle));
            first_time = false;
        }

        dtlog_info(TAG, "waiting for advertising restart on self %p semaphore %p", self, &self->advertiser_restart_semaphore);
        k_sem_take(&self->advertiser_restart_semaphore, K_FOREVER);
    }

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtnetportal_btle_advertiser_task_entrypoint failed");
    }

    dtlog_info(TAG, "terminating task");

    return dterr;
}
