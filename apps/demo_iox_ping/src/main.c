#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dtuart_helpers.h>
#include <dtmc_base_demos/demo_iox.h>

#include <dtmc/dtiox_zephyr_uartirq.h>

#define TAG "main"

// some local macros referring to the current demo to make it a bit easier to read the code
#define demo_t dtmc_base_demo_iox_t
#define demo_config_t dtmc_base_demo_iox_config_t
#define demo_create dtmc_base_demo_iox_create
#define demo_configure dtmc_base_demo_iox_configure
#define demo_start dtmc_base_demo_iox_start
#define demo_dispose dtmc_base_demo_iox_dispose

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtiox_handle uart_handle = NULL;
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
        uart_handle = (dtiox_handle)o;

        dtiox_zephyr_uartirq_config_t c = { 0 };
        c.device_tree_name = "demo_uart";
        c.uart_config = dtuart_helper_default_config;

        DTERR_C(dtiox_zephyr_uartirq_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.uart_handle = uart_handle;
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

    // dispose the UART instance
    dtiox_dispose(uart_handle);

    return rc;
}
