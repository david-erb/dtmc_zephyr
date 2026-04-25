#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys_clock.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dtbytes.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>
#include <dtcore/dttimeout.h>

#include <dtmc_base/dtbufferqueue.h>

#define TAG "dtbufferqueue"
#define dtlog_debug(TAG, ...)

// --------------------------------------------------------------------------------------
typedef struct dtbufferqueue_t
{
    int32_t max_count;
    bool should_overwrite;

    struct k_msgq msgq; // Zephyr message queue for the bufferqueue
    void* msgq_memory;

    bool is_msgq_initialized;

} dtbufferqueue_t;

// --------------------------------------------------------------------------------------
extern dterr_t*
dtbufferqueue_create(dtbufferqueue_handle* out_handle, int32_t max_count, bool should_overwrite)
{
    dterr_t* dterr = NULL;
    dtbufferqueue_t* self = NULL;
    DTERR_ASSERT_NOT_NULL(out_handle);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtbufferqueue_t), "dtbufferqueue_t", (void**)&self));

    self->max_count = max_count;
    self->should_overwrite = should_overwrite;

    if (self->max_count == 0)
        self->max_count = 1;

    size_t msg_size = sizeof(dtbuffer_t*); // each item is a pointer
    size_t msgq_memory_size = self->max_count * msg_size;
    DTERR_C(dtheaper_alloc_and_zero(msgq_memory_size, "dtbufferqueue msgq memory", (void**)&self->msgq_memory));
    k_msgq_init(&self->msgq, self->msgq_memory, msg_size, self->max_count);
    self->is_msgq_initialized = true;

    *out_handle = (dtbufferqueue_handle)self;

cleanup:
    if (dterr != NULL)
        dtbufferqueue_dispose((dtbufferqueue_handle)self);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtbufferqueue_put(dtbufferqueue_handle handle DTBUFFERQUEUE_PUT_ARGS)
{
    dterr_t* dterr = NULL;
    dtbufferqueue_t* self = (dtbufferqueue_t*)handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buffer);

    k_timeout_t ticks;
    if (timeout_millis == DTTIMEOUT_FOREVER)
    {
        ticks = K_FOREVER;
    }
    else if (timeout_millis == 0)
    {
        ticks = K_NO_WAIT;
    }
    else
    {
        ticks = K_MSEC(timeout_millis);
    }

#ifndef dtlog_debug
    {
        char tmp[64];
        dtbytes_compose_hex(((char*)buffer->payload), buffer->length, tmp, sizeof(tmp));
        dtlog_debug(TAG, "k_msgq_put pushing buffer %p length %" PRId32 " %s", buffer, buffer->length, tmp);
    }
#endif

    DTLOG_HERE();
    int ret = k_msgq_put(&self->msgq, &buffer, ticks);
    DTLOG_HERE();

    if (ret == -ENOMSG)
    {
        if (was_timeout)
            *was_timeout = true;
        goto cleanup;
    }

    if (ret == -EAGAIN)
    {
        if (was_timeout)
            *was_timeout = true;
        goto cleanup;
    }

    if (ret != 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "k_msgq_put got error %d", ret);
        goto cleanup;
    }

    if (was_timeout)
        *was_timeout = false;

cleanup:

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtbufferqueue_get(dtbufferqueue_handle handle DTBUFFERQUEUE_GET_ARGS)
{
    dterr_t* dterr = NULL;
    dtbufferqueue_t* self = (dtbufferqueue_t*)handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buffer);

    k_timeout_t ticks;
    if (timeout_millis == DTTIMEOUT_FOREVER)
    {
        ticks = K_FOREVER;
    }
    else if (timeout_millis == 0)
    {
        ticks = K_NO_WAIT;
    }
    else
    {
        ticks = K_MSEC(timeout_millis);
    }

    int ret = k_msgq_get(&self->msgq, buffer, ticks);
    if (ret == -ENOMSG)
    {
        if (was_timeout)
            *was_timeout = true;
        goto cleanup;
    }

    if (ret == -EAGAIN)
    {
        if (was_timeout)
            *was_timeout = true;
        goto cleanup;
    }

    if (ret != 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "k_msgq_get got error %d", ret);
        goto cleanup;
    }

#ifndef dtlog_debug
    {
        char tmp[64];
        dtbytes_compose_hex(((char*)(*buffer)->payload), (*buffer)->length, tmp, sizeof(tmp));
        dtlog_debug(TAG, "k_msgq_get returned buffer %p length %" PRId32 " %s", *buffer, (*buffer)->length, tmp);
    }
#endif

    if (was_timeout)
        *was_timeout = false;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtbufferqueue_dispose(dtbufferqueue_handle handle)
{
    dtbufferqueue_t* self = (dtbufferqueue_t*)handle;

    if (self == NULL)
        return;

    if (self->is_msgq_initialized)
    {
        k_msgq_purge(&self->msgq);
    }

    dtheaper_free(self->msgq_memory);
    dtheaper_free(self);
}
