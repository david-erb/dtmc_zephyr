#include <string.h>

#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>

#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtmc_base_constants.h>
#include <dtmc_base/dtinterval.h>

#include <dtmc/dtinterval_zephyr_counter.h>

#define TAG "dtinterval_zephyr_counter"

DTINTERVAL_INIT_VTABLE(dtinterval_zephyr_counter);

struct dtinterval_zephyr_counter_t
{
    DTINTERVAL_COMMON_MEMBERS;
    dtinterval_zephyr_counter_config_t config;
    bool _is_malloced;

    struct k_sem tick_sem;     // alarm ISR posts; start() dispatch loop takes
    uint32_t     _period_ticks; // config.periodic_interval_micros scaled to counter freq
    uint32_t     _next_alarm;   // absolute counter value for next tick (uint32 wrap-safe)

    dtinterval_callback_fn callback_fn;
    void*                  callback_context;
    bool                   _should_pause;
};

// --------------------------------------------------------------------------------------

static bool vtables_are_registered = false;

dterr_t*
dtinterval_zephyr_counter_register_vtables(void)
{
    dterr_t* dterr = NULL;

    if (!vtables_are_registered)
    {
        DTERR_C(dtinterval_set_vtable(DTMC_BASE_CONSTANTS_INTERVAL_MODEL_ZEPHYR_COUNTER,
                                      &dtinterval_zephyr_counter_vt));
        vtables_are_registered = true;
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// Alarm callback (ISR context).
//
// Re-arms at the next absolute target before signalling the dispatch thread.
// Using absolute timestamps means a delayed ISR shifts only that one tick, not all
// subsequent ones. COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE fires immediately if the
// target has already passed (e.g. after a long ISR), preventing a missed period.

static void
_alarm_handler(const struct device* dev, uint8_t chan, uint32_t ticks, void* user_data)
{
    dtinterval_zephyr_counter_t* self = (dtinterval_zephyr_counter_t*)user_data;

    self->_next_alarm += self->_period_ticks;
    struct counter_alarm_cfg cfg = {
        .callback  = _alarm_handler,
        .ticks     = self->_next_alarm,
        .user_data = self,
        .flags     = COUNTER_ALARM_CFG_ABSOLUTE | COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE,
    };
    counter_set_channel_alarm(dev, 0, &cfg);

    k_sem_give(&self->tick_sem);
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_counter_create(dtinterval_zephyr_counter_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtinterval_zephyr_counter_t),
                                    "dtinterval_zephyr_counter_t",
                                    (void**)self_ptr));
    DTERR_C(dtinterval_zephyr_counter_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr)
    {
        dtheaper_free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtinterval_zephyr_counter_create failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_counter_init(dtinterval_zephyr_counter_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_INTERVAL_MODEL_ZEPHYR_COUNTER;

    k_sem_init(&self->tick_sem, 0, 1);

    DTERR_C(dtinterval_zephyr_counter_register_vtables());

cleanup:
    if (dterr)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtinterval_zephyr_counter_init failed");
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_counter_configure(dtinterval_zephyr_counter_t* self,
                                    const dtinterval_zephyr_counter_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);
    DTERR_ASSERT_NOT_NULL(config->counter_dev);

    if (config->periodic_interval_micros <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "periodic_interval_micros must be > 0");
        goto cleanup;
    }

    self->config = *config;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_counter_set_callback(dtinterval_zephyr_counter_t* self DTINTERVAL_SET_CALLBACK_ARGS)
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
// Arms the counter peripheral, then blocks dispatching ticks to the callback.
// IPC: _alarm_handler (ISR context) posts tick_sem once per period; this loop
// takes it, confirming a real tick before invoking the callback. pause() cancels
// the alarm, stops the counter, and gives tick_sem to unblock a waiting take.

dterr_t*
dtinterval_zephyr_counter_start(dtinterval_zephyr_counter_t* self DTINTERVAL_START_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    uint32_t freq = counter_get_frequency(self->config.counter_dev);
    if (freq == 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "counter_get_frequency returned 0");
        goto cleanup;
    }

    self->_period_ticks =
      (uint32_t)((uint64_t)freq * (uint64_t)self->config.periodic_interval_micros / 1000000ULL);
    if (self->_period_ticks == 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "interval rounds to 0 counter ticks");
        goto cleanup;
    }

    self->_should_pause = false;

    int rc = counter_start(self->config.counter_dev);
    if (rc != 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "counter_start failed");
        goto cleanup;
    }

    counter_get_value(self->config.counter_dev, &self->_next_alarm);
    self->_next_alarm += self->_period_ticks;

    struct counter_alarm_cfg cfg = {
        .callback  = _alarm_handler,
        .ticks     = self->_next_alarm,
        .user_data = self,
        .flags     = COUNTER_ALARM_CFG_ABSOLUTE,
    };
    rc = counter_set_channel_alarm(self->config.counter_dev, 0, &cfg);
    if (rc != 0)
    {
        counter_stop(self->config.counter_dev);
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "counter_set_channel_alarm failed");
        goto cleanup;
    }

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
    counter_cancel_channel_alarm(self->config.counter_dev, 0);
    counter_stop(self->config.counter_dev);
    return dterr;
}

// --------------------------------------------------------------------------------------

dterr_t*
dtinterval_zephyr_counter_pause(dtinterval_zephyr_counter_t* self DTINTERVAL_PAUSE_ARGS)
{
    if (!self)
        return NULL;

    self->_should_pause = true;
    counter_cancel_channel_alarm(self->config.counter_dev, 0);
    counter_stop(self->config.counter_dev);
    k_sem_give(&self->tick_sem); // unblock start()'s dispatch loop
    return NULL;
}

// --------------------------------------------------------------------------------------

void
dtinterval_zephyr_counter_dispose(dtinterval_zephyr_counter_t* self)
{
    if (!self)
        return;

    if (self->config.counter_dev)
    {
        counter_cancel_channel_alarm(self->config.counter_dev, 0);
        counter_stop(self->config.counter_dev);
    }

    if (self->_is_malloced)
        dtheaper_free(self);
    else
        memset(self, 0, sizeof(*self));
}
