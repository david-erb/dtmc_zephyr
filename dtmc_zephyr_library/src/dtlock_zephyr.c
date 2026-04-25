#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <dtcore/dterr.h>
#include <dtmc_base/dtlock.h>

#define TAG "dtlock_zephyr"

// -------------------------------------------------------------------------------------------------
// Private implementation
typedef struct dtlock_zephyr_t
{
    struct k_mutex mutex;

    bool _is_malloced;
    bool _is_mutex_initialized;
} dtlock_zephyr_t;

// -------------------------------------------------------------------------------------------------
dterr_t*
dtlock_create(dtlock_handle* out_self)
{
    dterr_t* dterr = NULL;
    dtlock_zephyr_t* self = NULL;

    DTERR_ASSERT_NOT_NULL(out_self);

    *out_self = NULL;

    self = (dtlock_zephyr_t*)malloc(sizeof(dtlock_zephyr_t));
    if (self == NULL)
    {
        dterr = dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc %zu", sizeof(dtlock_zephyr_t));
        goto cleanup;
    }

    memset(self, 0, sizeof(*self));
    self->_is_malloced = true;

    k_mutex_init(&self->mutex);
    self->_is_mutex_initialized = true;

    *out_self = (dtlock_handle)self;

    self = NULL; // ownership transferred

cleanup:
    if (dterr != NULL)
    {
        if (self != NULL)
        {
            dtlock_dispose((dtlock_handle)self);
            self = NULL;
        }
    }
    return dterr;
}

// -------------------------------------------------------------------------------------------------
dterr_t*
dtlock_acquire(dtlock_handle handle)
{
    dterr_t* dterr = NULL;
    dtlock_zephyr_t* self = (dtlock_zephyr_t*)handle;

    DTERR_ASSERT_NOT_NULL(self);

    if (!self->_is_mutex_initialized)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "lock not initialized");
        goto cleanup;
    }

    DTERR_NEGERROR_C(k_mutex_lock(&self->mutex, K_FOREVER));

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------------------------
dterr_t*
dtlock_release(dtlock_handle handle)
{
    dterr_t* dterr = NULL;
    dtlock_zephyr_t* self = (dtlock_zephyr_t*)handle;

    DTERR_ASSERT_NOT_NULL(self);

    // tolerant if never initialized / already disposed
    if (!self->_is_mutex_initialized)
        goto cleanup;

    // Natural Zephyr behavior:
    // - if current thread does not own the mutex, rc != 0 (e.g., -EPERM)
    // - if mutex not locked, Zephyr may also return an error
    DTERR_NEGERROR_C(k_mutex_unlock(&self->mutex));

cleanup:
    return dterr;
}

// -------------------------------------------------------------------------------------------------
void
dtlock_dispose(dtlock_handle handle)
{
    dtlock_zephyr_t* self = (dtlock_zephyr_t*)handle;
    if (self == NULL)
        return;

    // Implies release (best-effort).
    if (self->_is_mutex_initialized)
    {
        k_mutex_unlock(&self->mutex);
    }

    bool is_malloced = self->_is_malloced;
    memset(self, 0, sizeof(*self));

    if (is_malloced)
        free(self);
}
