#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys_clock.h>
#include <zephyr/version.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dttasker.h>
#include <dtmc_base/dttasker_registry.h>

#include <dtmc/dtmc_zephyr.h>

// Zephyr: CONFIG_* is 1 or undefined; IS_ENABLED() -> 1 or 0
#define DTRUNTIME_CONCAT_CONFIG_FLAG(SYM)                                                                                      \
    do                                                                                                                         \
    {                                                                                                                          \
        const char* v = IS_ENABLED(SYM) ? "y" : "n";                                                                           \
        s = dtstr_concat_format(s, t, item_format_str, p, #SYM, v);                                                            \
    } while (0)

#define DTRUNTIME_ENQUOTE(X) #X

#define TAG "dtruntime_zephyr"

// --------------------------------------------------------------------------------------
const char*
dtruntime_flavor(void)
{
    return DTMC_ZEPHYR_FLAVOR;
}

// --------------------------------------------------------------------------------------
const char*
dtruntime_version(void)
{
    return DTMC_ZEPHYR_VERSION;
}

// --------------------------------------------------------------------------------------
bool
dtruntime_is_qemu()
{
    return false;
}

// --------------------------------------------------------------------------------------
extern dtruntime_milliseconds_t
dtruntime_now_milliseconds()
{
    return (dtruntime_milliseconds_t)k_uptime_get();
}

// --------------------------------------------------------------------------------------
extern void
dtruntime_sleep_milliseconds(dtruntime_milliseconds_t milliseconds)
{
    if (milliseconds <= 0)
    {
        // give scheduler a chance to run lower-priority threads
        k_yield();
    }
    else
    {
        k_msleep(milliseconds);
    }
}

// --------------------------------------------------------------------------------------
dterr_t*
dtruntime_format_environment_as_table(char** out_string)
{
    dterr_t* dterr = NULL;
    *out_string = NULL;

    char* p = "    ";
    char* s = NULL;
    char* t = "\n";

    const char* item_format_str = "%s%-40s %-24s";
    const char* item_format_int = "%s%-40s %" PRIu64;

    s = dtstr_concat_format(s, t, item_format_str, p, "dtmc_flavor", dtmc_flavor());
    s = dtstr_concat_format(s, t, item_format_str, p, "dtmc_version", dtmc_version());

    s = dtstr_concat_format(s, t, item_format_int, p, "zephyr ZEPHYR_VERSION_CODE", (uint64_t)ZEPHYR_VERSION_CODE);
    s = dtstr_concat_format(s, t, item_format_str, p, "zephyr KERNEL_VERSION_STRING", KERNEL_VERSION_STRING);
    s = dtstr_concat_format(s, t, item_format_str, p, "zephyr BUILD_VERSION", DTRUNTIME_ENQUOTE(BUILD_VERSION));

    s = dtstr_concat_format(s, t, item_format_str, p, "CONFIG_BOARD", CONFIG_BOARD);
    s = dtstr_concat_format(s, t, item_format_str, p, "CONFIG_ARCH", CONFIG_ARCH);

    DTRUNTIME_CONCAT_CONFIG_FLAG(CONFIG_ARM_ARCH_TIMER);
    DTRUNTIME_CONCAT_CONFIG_FLAG(CONFIG_NRF_RTC_TIMER);
    DTRUNTIME_CONCAT_CONFIG_FLAG(CONFIG_SCHED_THREAD_USAGE);
    DTRUNTIME_CONCAT_CONFIG_FLAG(CONFIG_SCHED_THREAD_USAGE_ANALYSIS);
    DTRUNTIME_CONCAT_CONFIG_FLAG(CONFIG_SYS_CLOCK_EXISTS);
    DTRUNTIME_CONCAT_CONFIG_FLAG(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER);
    DTRUNTIME_CONCAT_CONFIG_FLAG(CONFIG_DYNAMIC_THREAD);

    s =
      dtstr_concat_format(s, t, item_format_int, p, "CONFIG_SYS_CLOCK_TICKS_PER_SEC", (uint64_t)CONFIG_SYS_CLOCK_TICKS_PER_SEC);
    s = dtstr_concat_format(s, t, item_format_int, p, "sys_clock_hw_cycles_per_sec()", (uint64_t)sys_clock_hw_cycles_per_sec());
#ifdef CONFIG_CPU_CORTEX_M
    s = dtstr_concat_format(s, t, item_format_int, p, "ARM SystemCoreClock", (uint64_t)SystemCoreClock);
#endif
    s = dtstr_concat_format(s, t, item_format_int, p, "k_uptime_get()", (uint64_t)k_uptime_get());
    s = dtstr_concat_format(s, t, item_format_int, p, "k_cycle_get_64()", (uint64_t)k_cycle_get_64());

    goto cleanup;

cleanup:
    if (dterr != NULL)
    {
        free(s);
        s = NULL;
    }

    *out_string = s;

    return dterr;
}

// --------------------------------------------------------------------------------------------
void
dtruntime_log_environment(const char* tag, dtlog_level_t log_level, const char* prefix)
{
    dterr_t* dterr = NULL;
    char* s = NULL;

    dtruntime_format_environment_as_table(&s);

    if (dterr == NULL)
        dtlog_info(tag, "%s:\n%s", prefix, s);
    else
        dtlog_error(tag, "%s: failed to format runtime environment: %s", prefix, dterr->message);

    dtstr_dispose(s);
}

// --------------------------------------------------------------------------------------
static const char*
_device_api_type(const struct device* dev)
{

    return "Other";
}

// --------------------------------------------------------------------------------------
dterr_t*
dtruntime_format_devices_as_table(char** out_string)
{
    dterr_t* dterr = NULL;
    *out_string = NULL;
    const struct device* devs;
    int32_t count;

    char* p = "    ";
    char* s = NULL;
    char* t = "\n";

    const char* item_format_str = "%s%3" PRId32 ". %s %-40s %s";

    count = (int32_t)z_device_get_all_static(&devs);

    for (int32_t i = 0; i < count; ++i)
    {
        const struct device* dev = &devs[i];
        const char* ready = device_is_ready(dev) ? "✓" : "✗";
        const char* name = dev->name ? dev->name : "(null)";
        const char* type = _device_api_type(dev);
        s = dtstr_concat_format(s, t, item_format_str, p, i, ready, name, type);
    }

    goto cleanup;

cleanup:
    if (dterr != NULL)
    {
        free(s);
        s = NULL;
    }

    *out_string = s;

    return dterr;
}

// --------------------------------------------------------------------------------------------
void
dtruntime_log_devices(const char* tag, dtlog_level_t log_level, const char* prefix)
{
    dterr_t* dterr = NULL;
    char* s = NULL;

    dtruntime_format_devices_as_table(&s);

    if (dterr == NULL)
        dtlog_info(tag, "%s:\n%s", prefix, s);
    else
        dtlog_error(tag, "%s: failed to format runtime devices: %s", prefix, dterr->message);

    dtstr_dispose(s);
}

// --------------------------------------------------------------------------------------
#ifdef CONFIG_THREAD_MONITOR

static dterr_t*
noop_tasker_entry_point_fn(void* arg, dttasker_handle handle)
{
    (void)arg;
    (void)handle;
    return NULL;
}

// callback invoked for each thread in the system
static void
thread_report_cb(const struct k_thread* const_k_thread, void* caller_context)
{
    dterr_t* dterr = NULL;
    struct k_thread* thread = (struct k_thread*)const_k_thread;
    dttasker_registry_t* registry = (dttasker_registry_t*)caller_context;

    const char* thread_name = k_thread_name_get(thread);
    char thread_name_buf[32];
    if (thread_name == NULL || strlen(thread_name) == 0)
    {
        snprintf(thread_name_buf, sizeof(thread_name_buf), "0x%p", thread);
        thread_name = thread_name_buf;
    }

    dtguid_t guid;
    dtguid_generate_from_string(&guid, thread_name);
    char guid_str[37];
    dtguid_to_string(&guid, guid_str, sizeof(guid_str));

    dttasker_handle tasker_handle = NULL;
    DTERR_C(dtguidable_pool_search(&registry->pool, &guid, (dtguidable_handle*)&tasker_handle));
    if (tasker_handle != NULL)
    {
        // dtlog_debug(TAG, "%s(): updating thread task \"%s\" (%s) already in registry", __func__, thread_name, guid_str);
    }
    else
    {
        dttasker_config_t c = { 0 };
        c.name = thread_name;
        c.tasker_entry_point_fn = noop_tasker_entry_point_fn;
        c.stack_size = 1;
        // creaete a dummy task object to represent this thread
        DTERR_C(dttasker_create(&tasker_handle, &c));

        DTERR_C(dttasker_registry_insert(registry, tasker_handle));
    }

    // get the information on the task that we have been keeping track of
    dttasker_info_t info = { 0 };
    DTERR_C(dttasker_get_info(tasker_handle, &info));

    // keep the same thread name always
    strncpy(info._name, thread_name, sizeof(info._name) - 1);
    info._name[sizeof(info._name) - 1] = '\0';
    info.name = info._name;

    // presume it's running
    info.status = RUNNING;
    info.priority = k_thread_priority_get((k_tid_t)thread);

    k_thread_runtime_stats_t stats;
    k_thread_runtime_stats_get((k_tid_t)thread, &stats);

    // current time when we're checking
    dtcpu_microseconds_t time_microseconds = k_uptime_get() * 1000;
    // cumulative cpu used time as of now
#ifdef CONFIG_SCHED_THREAD_USAGE
    dtcpu_microseconds_t used_microseconds = (stats.execution_cycles * 1000000) / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
#else
    dtcpu_microseconds_t used_microseconds = 0;
#endif

    // deltas for cpu usage calculation
    dtcpu_microseconds_t delta_time_microseconds = time_microseconds - info.time_microseconds;
    dtcpu_microseconds_t delta_used_microseconds = used_microseconds - info.used_microseconds;

    // save these for next time
    info.time_microseconds = time_microseconds;
    info.used_microseconds = used_microseconds;

    info.cpu_percent_used = 0;
    if (info.time_microseconds == 0)
    {
        info.cpu_percent_used = -2;
    }
    else if (delta_time_microseconds == 0)
    {
        info.cpu_percent_used = -1;
    }
    else
    {
        info.cpu_percent_used = (100 * delta_used_microseconds) / delta_time_microseconds;
    }

    {
#if defined(CONFIG_THREAD_STACK_INFO) && !defined(CONFIG_ARCH_POSIX)
        int rc;
        size_t size;
        rc = k_thread_stack_space_get((k_tid_t)thread, &size);
        if (rc == 0)
            info.stack_used_bytes = size;
        else
            info.stack_used_bytes = rc;
#endif
    }

    DTERR_C(dttasker_set_info(tasker_handle, &info));

cleanup:

    if (dterr != NULL)
        dtlog_error(TAG, "%s(): error processing thread \"%s\": %s", __func__, thread_name, dterr->message);
    return;
}

#endif // CONFIG_THREAD_MONITOR

// --------------------------------------------------------------------------------------

dterr_t*
dtruntime_register_tasks(dttasker_registry_t* registry)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(registry);

    // TODO: Add locking around registry auto-init in dtruntime_register_tasks().
    if (!registry->is_initialized)
    {
        DTERR_C(dttasker_registry_init(registry));
    }

    // iterate all threads
    // do it non-locking so irq-called threads like uart don't get frozen out of their oeprations
#ifdef CONFIG_THREAD_MONITOR
    k_thread_foreach_unlocked(thread_report_cb, registry);
#endif

cleanup:
    return dterr;
}