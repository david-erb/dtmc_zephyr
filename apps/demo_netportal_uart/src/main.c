#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc_base/dtframer.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

// we can cover these concrete objects platform agnostically
#include <dtmc_base/dtframer_simple.h>
#include <dtmc_base/dtnetportal_iox.h>

// these concrete objects are platform specific
#include <dtmc/dtiox_zephyr_uartirq.h>

#include <dtmc_base_demos/demo_netportal.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtiox_handle iox_handle = NULL;
    dtframer_handle framer_handle = NULL;
    dtnetportal_handle netportal_handle = NULL;

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
        dtiox_zephyr_uartirq_t* o = NULL;
        DTERR_C(dtiox_zephyr_uartirq_create(&o));
        iox_handle = (dtiox_handle)o;

        dtiox_zephyr_uartirq_config_t c = { 0 };

        // device tree label from boards/<boardname>.overlay.  Pins are configured there too.
        // choose your device tree node name, e.g., "CDC_ACM_0" or "uart0"
        // c.device_tree_name = "demo_uart";
        c.device_tree_name = "CDC_ACM_0";

        c.uart_config = dtuart_helper_default_config;
        c.tx_capacity = 128;
        c.rx_capacity = 128;

        DTERR_C(dtiox_zephyr_uartirq_configure(o, &c));
    }

    // === the framer ===
    {
        dtframer_simple_t* o = NULL;
        DTERR_C(dtframer_simple_create(&o));
        framer_handle = (dtframer_handle)o;
        dtframer_simple_config_t c = { 0 };
        DTERR_C(dtframer_simple_configure(o, &c));
    }
    // === the netportal ===
    {
        dtnetportal_iox_t* o = NULL;
        DTERR_C(dtnetportal_iox_create(&o));
        netportal_handle = (dtnetportal_handle)o;
        dtnetportal_iox_config_t c = { 0 };
        c.iox_handle = iox_handle;
        c.framer_handle = framer_handle;
        DTERR_C(dtnetportal_iox_configure(o, &c));
    }
    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.netportal_handle = netportal_handle;
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

    dtnetportal_dispose(netportal_handle);

    dtframer_dispose(framer_handle);

    dtiox_dispose(iox_handle);

    return rc;
}
