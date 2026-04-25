#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys_clock.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>
#include <dtcore/dttimeout.h>

#include <dtmc_base/dtsemaphore.h>

#define TAG "dtsemaphore_zephyr"

// --------------------------------------------------------------------------------------
typedef struct dtsemaphore_zephyr_t
{
    bool is_semaphore_initialized;
    struct k_sem semaphore; // Semaphore for notification

    uint32_t count;     // current semaphore count
    uint32_t max_count; // 0 means "no explicit cap" (use INT32_MAX)

} dtsemaphore_zephyr_t;

// --------------------------------------------------------------------------------------
dterr_t*
dtsemaphore_create(dtsemaphore_handle* self_handle, int32_t initial_count, int32_t max_count)
{
    dterr_t* dterr = NULL;
    dtsemaphore_zephyr_t* self = NULL;
    int err;
    DTERR_ASSERT_NOT_NULL(self_handle);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtsemaphore_zephyr_t), "dtsemaphore_zephyr_t", (void**)&self));

    self->max_count = (max_count == 0) ? INT32_MAX : max_count;
    self->count = initial_count;

    err = k_sem_init( //
      &self->semaphore,
      self->count,
      self->max_count);

    if (err != 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "k_sem_init failed: %d", err);
        goto cleanup;
    }

    self->is_semaphore_initialized = true;

    *self_handle = (dtsemaphore_handle)self;

cleanup:

    if (dterr != NULL)
    {
        dtsemaphore_dispose((dtsemaphore_handle)self);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtsemaphore_post(dtsemaphore_handle self_handle)
{
    dterr_t* dterr = NULL;
    dtsemaphore_zephyr_t* self = (dtsemaphore_zephyr_t*)self_handle;

    // this needs to be ISR safe so no making a dynamic error object here
    if (self != NULL && self->is_semaphore_initialized)
    {
        k_sem_give(&self->semaphore);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtsemaphore_wait(dtsemaphore_handle self_handle, dttimeout_millis_t timeout_milliseconds, bool* was_timeout)
{
    dterr_t* dterr = NULL;
    dtsemaphore_zephyr_t* self = (dtsemaphore_zephyr_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    if (!self->is_semaphore_initialized)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "semaphore not initialized");
        goto cleanup;
    }

    k_timeout_t timeout;
    if (timeout_milliseconds == DTTIMEOUT_FOREVER)
    {
        timeout = K_FOREVER;
    }
    else if (timeout_milliseconds == DTTIMEOUT_NOWAIT)
    {
        timeout = K_NO_WAIT;
    }
    else
    {
        timeout = K_MSEC(timeout_milliseconds);
    }

    int err = k_sem_take(&self->semaphore, timeout);
    if (err < 0)
    {
        if (err == -EAGAIN)
        {
            if (was_timeout != NULL)
                *was_timeout = true;
            else
                dterr = dterr_new(DTERR_TIMEOUT,
                  DTERR_LOC,
                  NULL,
                  "semaphore wait timed out after %" DTTIMEOUT_MILLIS_PRI " ms",
                  timeout_milliseconds);
        }
        else
        {
            dterr =
              dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "semaphore wait failed: " DTERR_NEGERROR_FORMAT, DTERR_NEGERROR_ARGS(err));
        }
        goto cleanup;
    }
    if (was_timeout != NULL)
    {
        *was_timeout = false;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// best-effort teardown (OK even if never initialized beyond zeroed memory)

void
dtsemaphore_dispose(dtsemaphore_handle self_handle)
{
    dtsemaphore_zephyr_t* self = NULL;
    self = (dtsemaphore_zephyr_t*)self_handle;

    if (self == NULL)
        return;

    dtheaper_free(self);
}
