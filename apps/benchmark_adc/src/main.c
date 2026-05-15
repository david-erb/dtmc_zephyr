#include <zephyr/device.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtadc.h>
#include <dtmc_base/dtruntime.h>

#include <dtmc/dtadc_zephyr_saadc.h>

#include <dtmc_base_benchmarks/benchmark_adc.h>

#define TAG "main"

#define BENCHMARK_SCAN_INTERVAL_MS 10 // 100Hz
#define BENCHMARK_SCAN_INTERVAL_US (BENCHMARK_SCAN_INTERVAL_MS * 1000)

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtadc_zephyr_saadc_t* adc = NULL;
    benchmark_t* benchmark = NULL;

    {
        char* s = NULL;
        DTERR_C(dtruntime_format_devices_as_table(&s));
        dtlog_info(TAG, "devices:\n%s", s);
        dtstr_dispose(s);
    }
    {
        char* s = NULL;
        DTERR_C(dtruntime_format_environment_as_table(&s));
        dtlog_info(TAG, "environment:\n%s", s);
        dtstr_dispose(s);
    }

    // === create and configure the ADC ===
    {
        DTERR_C(dtadc_zephyr_saadc_create(&adc));

        dtadc_zephyr_saadc_config_t c = { 0 };
        dtadc_zephyr_saadc_config_init_defaults(&c);
        c.scan_interval_ms = BENCHMARK_SCAN_INTERVAL_MS;
        c.counter_dev = DEVICE_DT_GET(DT_NODELABEL(timer1));

        DTERR_C(dtadc_zephyr_saadc_configure(adc, &c));
    }

    // === create and configure the benchmark ===
    {
        DTERR_C(benchmark_create(&benchmark));

        benchmark_config_t c = { 0 };
        c.adc_handle = (dtadc_handle)adc;
        c.nominal_scan_interval_us = BENCHMARK_SCAN_INTERVAL_US;
        c.app_core = 0;

        DTERR_C(benchmark_configure(benchmark, &c));
    }

    // === run the benchmark (blocks until collection is done, then prints results) ===
    DTERR_C(benchmark_start(benchmark));

cleanup:
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    benchmark_dispose(benchmark);
    dtadc_dispose((dtadc_handle)adc);

    exit(dterr ? -1 : 0);
}
