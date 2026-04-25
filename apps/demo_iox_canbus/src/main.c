#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base_demos/demo_iox.h>

#include <dtmc/dtiox_zephyr_canbus.h>

#define TAG "main"

int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtiox_handle can_handle = NULL;
    demo_t* demo = NULL;

    // ==== print the currently registered devices ====
    {
        char* s = NULL;
        DTERR_C(dtruntime_format_devices_as_table(&s));

        dtlog_info(TAG, "devices:\n%s", s);

        dtstr_dispose(s);
    }

    // === create the concrete can object we need ===
    {
        dtiox_zephyr_canbus_t* o = NULL;
        DTERR_C(dtiox_zephyr_canbus_create(&o));
        can_handle = (dtiox_handle)o;
        dtiox_zephyr_canbus_config_t c = { 0 };
        c.txid = 0x123;
        DTERR_C(dtiox_zephyr_canbus_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.iox_handle = can_handle;
        c.node_name = "zephyr";
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

    // dispose the can instance
    dtiox_dispose(can_handle);

    return rc;
}
