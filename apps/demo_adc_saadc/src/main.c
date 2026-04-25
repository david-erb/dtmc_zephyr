#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc_base/dtadc.h>
#include <dtmc_base/dtframer.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

// we can cover these concrete objects platform agnostically
#include <dtmc_base/dtadc.h>

// these concrete objects are platform specific
#include <dtmc/dtadc_zephyr_saadc.h>

#include <dtmc_base_demos/demo_adc_print.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtadc_handle adc_handle = NULL;

    demo_t* demo = NULL;

    // ==== print the currently registered devices ====
    {
        char* s = NULL;
        DTERR_C(dtruntime_format_devices_as_table(&s));

        dtlog_info(TAG, "devices:\n%s", s);

        dtstr_dispose(s);
    }

    // === create the concrete UART object we need ===
    {
        dtadc_zephyr_saadc_t* o = NULL;
        DTERR_C(dtadc_zephyr_saadc_create(&o));
        adc_handle = (dtadc_handle)o;

        dtadc_zephyr_saadc_config_t c = { 0 };
        dtadc_zephyr_saadc_config_init_defaults(&c);
        DTERR_C(dtadc_zephyr_saadc_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.adc_handle = adc_handle;
        c.scan_timeout_ms = 10000;
        c.max_scan_count = 10;
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

cleanup:
    int rc = (dterr != NULL) ? -1 : 0;

    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);

    dterr_dispose(dterr);

    // dispose the demo instance
    demo_dispose(demo);

    // dispose the objects

    dtadc_dispose(adc_handle);

    return rc;
}
