#include <inttypes.h>
#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtunittest.h>

#include <dtmc_base/dtlightsensor.h>
#include <dtmc_base/dtruntime.h>

#include <dtmc/dtlightsensor_zephyr.h>

#define TAG "test_dtlightsensor"

// ----------------------------------------------------------------------------------
// Unit test: Microsecond time should be monotonic
static dterr_t*
test_dtlightsensor_tsl2591()
{
    dterr_t* dterr = NULL;
    dtlightsensor_handle sensor_handle = NULL;

    {
        dtlightsensor_zephyr_t* o = NULL;
        DTERR_C(dtlightsensor_zephyr_create(&o));
        sensor_handle = (dtlightsensor_handle)o;
        dtlightsensor_zephyr_config_t c = { 0 };
        c.device_name = "TSL2591";
        DTERR_C(dtlightsensor_zephyr_configure(o, &c));
    }

    DTERR_C(dtlightsensor_activate(sensor_handle));

    {
        int64_t sample;
        for (int i = 0; i < 5; i++)
        {
            DTERR_C(dtlightsensor_sample(sensor_handle, &sample));
            dtlog_info(TAG, "%d. sample: %" PRId64, i, sample);
            dtruntime_sleep_milliseconds(1000);
        }
    }

    goto cleanup;
cleanup:
    dtlightsensor_dispose(sensor_handle);

    return dterr;
}

// --------------------------------------------------------------------------------------------
void
test_dtmc_zephyr_dtlightsensors(DTUNITTEST_SUITE_ARGS)
{
    DTUNITTEST_RUN_TEST(test_dtlightsensor_tsl2591);
}
