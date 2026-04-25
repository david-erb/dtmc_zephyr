#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/time_units.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtguid.h>
#include <dtcore/dtguidable.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dttasker.h>
#include <dtmc_base/dttasker_registry.h>

#include <dtmc/dtmc_zephyr.h>

DTGUIDABLE_INIT_VTABLE(dttasker);

#define dtlog_debug(TAG, ...)

typedef struct dttasker_t
{
    int32_t model_number;
    char _name[32]; // storage for name

    dttasker_config_t config;
    struct k_event event;
    dttasker_info_t info;
    struct k_thread thread_data;
    k_tid_t thread_id;
    bool _is_stack_malloced;
    void* stack_area;

    // lifecycle / stop state
    atomic_bool stop_requested;
    atomic_bool task_exited;
    atomic_bool task_created;
    atomic_bool task_joined;

    dtguid_t guid; // 16 bytes, does not need to be aligned
} dttasker_t;

#define TAG "dttasker"
#define TASK_STACK_ALIGN sizeof(void*) // Safe fallback

#define DTTASKER_EVENT_READY_BIT BIT(0)
#define DTTASKER_EVENT_EXITED_BIT BIT(1)

static dterr_t*
dttasker_priority_enum_to_native_number(dttasker_priority_t p, int32_t* native_number);

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_create(dttasker_handle* self_handle, dttasker_config_t* config)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = NULL;
    DTERR_ASSERT_NOT_NULL(self_handle);
    DTERR_ASSERT_NOT_NULL(config);
    DTERR_ASSERT_NOT_NULL(config->name);

    DTERR_C(dttasker_validate_priority_enum(config->priority));

    if (strlen(config->name) >= sizeof(self->_name))
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "config->name too long (max %zu)", sizeof(self->_name) - 1);
        goto cleanup;
    }

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dttasker_t), "dttasker_t", (void**)&self));

    self->model_number = DTMC_BASE_CONSTANTS_TASKER_MODEL_LINUX;

    self->config = *config;

    // put the config name into internal storage
    strncpy(self->_name, config->name, sizeof(self->_name) - 1);
    self->_name[sizeof(self->_name) - 1] = '\0';

    DTERR_C(dtguidable_set_vtable(self->model_number, &dttasker_guidable_vt));
    dtguid_generate_from_string(&self->guid, self->_name);

    // Mark initial state
    dttasker_info_t info = {
        .status = INITIALIZED,
    };
    DTERR_C(dttasker_set_info((dttasker_handle)self, &info));

    k_event_init(&self->event);

    atomic_init(&self->stop_requested, false);
    atomic_init(&self->task_exited, false);
    atomic_init(&self->task_created, false);
    atomic_init(&self->task_joined, false);

    *self_handle = (dttasker_handle)self;
cleanup:
    if (dterr != NULL)
        dttasker_dispose((dttasker_handle)self);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_set_entry_point(dttasker_handle self_handle, dttasker_entry_point_fn entry_point_function, void* entry_point_arg)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    self->config.tasker_entry_point_fn = entry_point_function;
    self->config.tasker_entry_point_arg = entry_point_arg;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
static void
dttasker__thread_inception_function(void* arg1, void* arg2, void* arg3)
{
    dterr_t* dterr = NULL;
    dttasker_handle self_handle = (dttasker_handle)arg1;
    dttasker_t* self = (dttasker_t*)arg1;

    k_thread_name_set(k_current_get(), self->_name);

    // blocking call doing main task logic
    DTERR_C(self->config.tasker_entry_point_fn(self->config.tasker_entry_point_arg, (dttasker_handle)self));

cleanup:

    dttasker_info_t info = { .status = STOPPED, .dterr = dterr };
    dttasker_set_info(self_handle, &info);

    // If start() is still waiting because the task failed before ready(), wake it.
    if (dterr)
    {
        uint32_t result = k_event_wait(&self->event, DTTASKER_EVENT_READY_BIT, false, K_NO_WAIT);

        if (!(result & DTTASKER_EVENT_READY_BIT))
        {
            dtlog_debug(TAG, "%s(): posting event (with error) for task \"%s\"", __func__, self->config.name);
            k_event_post(&self->event, DTTASKER_EVENT_READY_BIT);
        }
    }

    // Mark permanent exited state for join().
    atomic_store_explicit(&self->task_exited, true, memory_order_release);
    k_event_post(&self->event, DTTASKER_EVENT_EXITED_BIT);

    // Note: Zephyr threads are not automatically cleaned up when they finish; the
    // thread remains until explicitly joined or the system reclaims it on a context switch.
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_start(dttasker_handle self_handle)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->config.tasker_entry_point_fn);

    // Refuse to start again if task is already running / not yet join-complete.
    if (atomic_load_explicit(&self->task_created, memory_order_acquire) &&
        !atomic_load_explicit(&self->task_exited, memory_order_acquire))
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "task \"%s\" is already started", self->_name);
        goto cleanup;
    }

    // Clear any stale event bits from a prior run.
    k_event_set(&self->event, 0);

    atomic_store_explicit(&self->stop_requested, false, memory_order_release);
    atomic_store_explicit(&self->task_exited, false, memory_order_release);
    atomic_store_explicit(&self->task_joined, false, memory_order_release);
    atomic_store_explicit(&self->task_created, false, memory_order_release);

    size_t stack_size = self->config.stack_size;
    self->stack_area = k_malloc(stack_size);
    if (!self->stack_area)
    {
        dterr = dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "failed to alloc stack size %zu", stack_size);
        goto cleanup;
    }

    self->_is_stack_malloced = true;

    int32_t native_priority = 0;
    DTERR_C(dttasker_priority_enum_to_native_number(self->config.priority, &native_priority));

    dtlog_debug(TAG,
      "%s(): called for task %s priority %s (native %" PRId32 ")",
      __func__,
      self->config.name,
      dttasker_priority_enum_to_string(self->config.priority),
      native_priority);

    self->thread_id = k_thread_create(&self->thread_data,
      self->stack_area,
      stack_size,
      dttasker__thread_inception_function,
      self,
      NULL,
      NULL,
      native_priority,
      0,
      K_NO_WAIT);
    dtlog_debug(TAG, "%s(): created thread %p for task %s", __func__, self->thread_id, self->config.name);

    atomic_store_explicit(&self->task_created, true, memory_order_release);

    // Wait until task signals it started (or errored before ready())
    k_event_wait(&self->event, DTTASKER_EVENT_READY_BIT, false, K_FOREVER);
    dtlog_debug(TAG, "%s(): sees ready event for task %s", __func__, self->config.name);

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "%s failed for task %s", __func__, self->config.name);
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_ready(dttasker_handle self_handle)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    dttasker_info_t info = { .status = RUNNING };
    DTERR_C(dttasker_set_info(self_handle, &info));

    dtlog_debug(TAG, "%s(): posting event for task %s", __func__, self->config.name);
    k_event_post(&self->event, DTTASKER_EVENT_READY_BIT);
cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// request stop of the task (called from outside the task)
dterr_t*
dttasker_stop(dttasker_handle self_handle)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);

    atomic_store_explicit(&self->stop_requested, true, memory_order_release);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// wait for task to stop (called from outside the task)
dterr_t*
dttasker_join(dttasker_handle self_handle, dttimeout_millis_t timeout_millis, bool* was_timeout)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    k_timeout_t ticks = K_FOREVER;
    uint32_t bits = 0;

    DTERR_ASSERT_NOT_NULL(self);

    if (was_timeout != NULL)
    {
        *was_timeout = false;
    }

    // If never started or already joined, nothing to do.
    if (!atomic_load_explicit(&self->task_created, memory_order_acquire) ||
        atomic_load_explicit(&self->task_joined, memory_order_acquire))
    {
        goto cleanup;
    }

    // Fast path: task already exited.
    if (atomic_load_explicit(&self->task_exited, memory_order_acquire))
    {
        atomic_store_explicit(&self->task_joined, true, memory_order_release);
        atomic_store_explicit(&self->task_created, false, memory_order_release);
        goto cleanup;
    }

    if (timeout_millis != DTTIMEOUT_FOREVER)
    {
        ticks = K_MSEC((uint32_t)timeout_millis);

        // Avoid accidental zero-tick waits for small positive timeouts.
        if (timeout_millis > 0 && ticks.ticks == 0)
        {
            ticks.ticks = 1;
        }
    }

    bits = k_event_wait(&self->event, DTTASKER_EVENT_EXITED_BIT, false, ticks);

    if (!(bits & DTTASKER_EVENT_EXITED_BIT))
    {
        if (was_timeout != NULL)
        {
            *was_timeout = true;
        }
        else
        {
            dterr = dterr_new(DTERR_TIMEOUT,
              DTERR_LOC,
              NULL,
              "task \"%s\" did not join within timeout %" DTTIMEOUT_MILLIS_PRI " milliseconds",
              self->_name,
              timeout_millis);
        }
        goto cleanup;
    }

    atomic_store_explicit(&self->task_joined, true, memory_order_release);
    atomic_store_explicit(&self->task_created, false, memory_order_release);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
// poll if task is supposed to stop (called from client code inside the task)
dterr_t*
dttasker_poll(dttasker_handle self_handle, bool* should_stop)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(should_stop);

    *should_stop = atomic_load_explicit(&self->stop_requested, memory_order_acquire);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_get_info(dttasker_handle self_handle, dttasker_info_t* info)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(info);

    *info = self->info;

    // always fill the info with the task's originally configured name
    info->name = info->_name;
    strncpy(info->name, self->_name, sizeof(self->info._name));
    info->name[sizeof(self->info._name) - 1] = '\0';

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_set_info(dttasker_handle self_handle, dttasker_info_t* info)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(info);

    // we absorb the info struct, but be aware we won't be using the name pointer as-is
    self->info = *info;

    // somebody wants us to call their callback on status changes
    if (self->config.tasker_info_callback != NULL)
    {
        dttasker_info_t callback_info = self->info;
        callback_info.name = callback_info._name;
        strncpy(callback_info.name, self->_name, sizeof(callback_info._name));
        callback_info.name[sizeof(callback_info._name) - 1] = '\0';
        DTERR_C(self->config.tasker_info_callback(self->config.tasker_info_callback_context, &callback_info));
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dttasker_get_guid(dttasker_handle self_handle, dtguid_t* guid)
{
    dterr_t* dterr = NULL;
    dttasker_t* self = (dttasker_t*)self_handle;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(guid);
    dtguid_copy(guid, &self->guid);
cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
void
dttasker_dispose(dttasker_handle self_handle)
{
    dttasker_t* self = (dttasker_t*)self_handle;
    if (!self)
        return;

    if (self->_is_stack_malloced && self->stack_area)
        k_free(self->stack_area);

    dtheaper_free(self);
}

// =============================================================================
// ZEPHYR: fixed mapping (no scaling)
// =============================================================================
//
// Requirements:
//   - Always preemptive priorities only
//   - Exactly 15 preempt priorities available
//   - Native Zephyr preempt priorities: [-15 .. -1]
//     (more negative => higher urgency)
//
// Mapping policy (15 levels total):
//   BACKGROUND_* -> -1 .. -5
//   NORMAL_*     -> -6 .. -10
//   URGENT_*     -> -11 .. -15
//

#if !defined(CONFIG_NUM_PREEMPT_PRIORITIES)
#error "CONFIG_NUM_PREEMPT_PRIORITIES must be defined (expected 15)."
#endif

#if (CONFIG_NUM_PREEMPT_PRIORITIES != 15)
#error "This project requires CONFIG_NUM_PREEMPT_PRIORITIES == 15 (fixed priority mapping)."
#endif

static dterr_t*
dttasker_priority_enum_to_native_number(dttasker_priority_t p, int32_t* native_priority)
{
    dterr_t* dterr;
    DTERR_ASSERT_NOT_NULL(native_priority);

    // in Zephyr, numerically lower priorities take precedence over numerically higher values
    // a preemptible thread has a non-negative priority value
    switch (p)
    {
        // Stable default: pick the middle of NORMAL band.
        case DTTASKER_PRIORITY_DEFAULT_FOR_SITUATION:
            *native_priority = 8; // NORMAL_MEDIUM
            return NULL;

        // BACKGROUND band
        case DTTASKER_PRIORITY_BACKGROUND_LOWEST:
            *native_priority = 14;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_LOW:
            *native_priority = 13;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_MEDIUM:
            *native_priority = 12;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_HIGH:
            *native_priority = 11;
            return NULL;
        case DTTASKER_PRIORITY_BACKGROUND_HIGHEST:
            *native_priority = 10;
            return NULL;

        // NORMAL band
        case DTTASKER_PRIORITY_NORMAL_LOWEST:
            *native_priority = 9;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_LOW:
            *native_priority = 8;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_MEDIUM:
            *native_priority = 7;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_HIGH:
            *native_priority = 6;
            return NULL;
        case DTTASKER_PRIORITY_NORMAL_HIGHEST:
            *native_priority = 5;
            return NULL;

        // URGENT band
        case DTTASKER_PRIORITY_URGENT_LOWEST:
            *native_priority = 4;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_LOW:
            *native_priority = 3;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_MEDIUM:
            *native_priority = 2;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_HIGH:
            *native_priority = 1;
            return NULL;
        case DTTASKER_PRIORITY_URGENT_HIGHEST:
            *native_priority = 0;
            return NULL;

        // markers / invalid
        case DTTASKER_PRIORITY__START:
        case DTTASKER_PRIORITY__COUNT:
        default:
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "unknown dttasker_priority_t value %" PRId32, (int32_t)p);
    }
cleanup:
    return dterr;
}
