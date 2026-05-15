#include <string.h>

#include <zephyr/kernel.h>

#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtmc_base_constants.h>
#include <dtmc_base/dtinterval.h>

#include <dtmc/dtinterval_zephyr_ktimer.h>

#define TAG "dtinterval_zephyr_ktimer"

DTINTERVAL_INIT_VTABLE(dtinterval_zephyr_ktimer);

struct dtinterval_zephyr_ktimer_t
{
    DTINTERVAL_COMMON_MEMBERS;
    dtinterval_zephyr_ktimer_config_t config;
    bool _is_malloced;

    struct k_timer timer;
    struct k_sem   tick_sem; // ISR posts; start() dispatch loop takes

    dtinterval_callback_fn callback_fn;
    void*                  callback_context;
    bool                   _should_pause;
};

// --------------------------------------------------------------------------------------

static bool vtables_are_registered = false;

dterr_t*
dtinterval_zephyr_ktimer_register_vtables(void)
{
    dterr_t* dterr = NULL;

    if (!vtables_are_registered)
    {
        DTERR_C(dtinterval_set_vtable(DTMC_BASE_CONSTANTS_INTERVAL_MODEL_ZEPHYR_KTIMER, &dtinterval_zephyr_ktimer_vt));
        vtables_are_registered = true;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------

static void
timer_expiry_fn(struct k_timer* timer)
{
    dtinterval_zephyr_ktimer_t* self = CONTAINER_OF(timer, dtinterval_zephyr_ktimer_t, timer);
    k_sem_give(&self->tick_sem);
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_ktimer_create(dtinterval_zephyr_ktimer_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtinterval_zephyr_ktimer_t), "dtinterval_zephyr_ktimer_t", (void**)self_ptr));
    DTERR_C(dtinterval_zephyr_ktimer_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr)
    {
        dtheaper_free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtinterval_zephyr_ktimer_create failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_ktimer_init(dtinterval_zephyr_ktimer_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_INTERVAL_MODEL_ZEPHYR_KTIMER;

    k_timer_init(&self->timer, timer_expiry_fn, NULL);
    k_sem_init(&self->tick_sem, 0, 1);

    DTERR_C(dtinterval_zephyr_ktimer_register_vtables());

cleanup:
    if (dterr)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtinterval_zephyr_ktimer_init failed");
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_ktimer_configure(dtinterval_zephyr_ktimer_t* self, const dtinterval_zephyr_ktimer_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);
    self->config = *config;
cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_ktimer_set_callback(dtinterval_zephyr_ktimer_t* self DTINTERVAL_SET_CALLBACK_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(callback);
    DTERR_ASSERT_NOT_NULL(context);
    self->callback_fn      = callback;
    self->callback_context = context;
cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// Arms the k_timer, then blocks dispatching ticks to the callback.
// IPC: timer_expiry_fn (ISR context) posts tick_sem once per period; this loop
// takes it, confirming a real tick before invoking the callback. pause() sets
// _should_pause and gives tick_sem to unblock a waiting take without a tick.

dterr_t*
dtinterval_zephyr_ktimer_start(dtinterval_zephyr_ktimer_t* self DTINTERVAL_START_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->_should_pause = false;
    k_timeout_t period  = K_USEC(self->config.periodic_interval_micros);
    k_timer_start(&self->timer, period, period);

    if (self->callback_fn != NULL)
    {
        while (!self->_should_pause)
        {
            k_sem_take(&self->tick_sem, K_FOREVER);
            if (self->_should_pause)
                break;

            int should_pause = 0;
            DTERR_C(self->callback_fn(self->callback_context, &should_pause));
            self->_should_pause = (bool)should_pause;
        }
    }

cleanup:
    if (dterr)
        k_timer_stop(&self->timer);
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_ktimer_pause(dtinterval_zephyr_ktimer_t* self DTINTERVAL_PAUSE_ARGS)
{
    if (!self)
        return NULL;

    self->_should_pause = true;
    k_timer_stop(&self->timer);
    k_sem_give(&self->tick_sem); // unblock start()'s dispatch loop
    return NULL;
}

// --------------------------------------------------------------------------------------

void
dtinterval_zephyr_ktimer_dispose(dtinterval_zephyr_ktimer_t* self)
{
    if (!self)
        return;

    k_timer_stop(&self->timer);

    if (self->_is_malloced)
        dtheaper_free(self);
    else
        memset(self, 0, sizeof(*self));
}
