#include <zephyr/device.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc/dtinterval_zephyr_counter.h>

#include <dtmc_base_benchmarks/benchmark_interval.h>

#define TAG "main"

#define BENCHMARK_INTERVAL_US 10000 // 10ms nominal period

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtinterval_zephyr_counter_t* interval = NULL;
    benchmark_t* benchmark = NULL;

    {
        char* s = NULL;
        DTERR_C(dtruntime_format_devices_as_table(&s));
        dtlog_info(TAG, "devices:\n%s", s);
        dtstr_dispose(s);
    }

    // === create and configure the interval ===
    {
        DTERR_C(dtinterval_zephyr_counter_create(&interval));

        dtinterval_zephyr_counter_config_t c = {
            .name = "benchmark",
            .periodic_interval_micros = BENCHMARK_INTERVAL_US,
            .counter_dev = DEVICE_DT_GET(DT_NODELABEL(timer1)),
        };
        DTERR_C(dtinterval_zephyr_counter_configure(interval, &c));
    }

    // === create and configure the benchmark ===
    {
        DTERR_C(benchmark_create(&benchmark));

        benchmark_config_t c = { 0 };
        c.interval_handle = (dtinterval_handle)interval;
        c.nominal_interval_us = BENCHMARK_INTERVAL_US;
        c.app_core = 0;

        DTERR_C(benchmark_configure(benchmark, &c));
    }

    // === run the benchmark (blocks until collection is done, then prints results) ===
    DTERR_C(benchmark_start(benchmark));

cleanup:
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    benchmark_dispose(benchmark);
    dtinterval_zephyr_counter_dispose(interval);

    exit(dterr ? -1 : 0);
}
