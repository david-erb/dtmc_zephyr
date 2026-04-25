#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dtuart_helpers.h>
#include <dtmc_base_demos/demo_iox.h>

#include <dtmc/dtiox_zephyr_modbus_slave.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtiox_handle iox_handle = NULL;
    demo_t* demo = NULL;

    // === create the concrete IOX object we need ===
    {
        dtiox_zephyr_modbus_slave_t* o = NULL;
        DTERR_C(dtiox_zephyr_modbus_slave_create(&o));
        iox_handle = (dtiox_handle)o;
        dtiox_zephyr_modbus_slave_config_t c = { 0 };
        c.device_tree_name = "demo_modbus";
        c.unit_id = 1;
        c.uart_config = dtuart_helper_default_config;
        c.rx_ring_capacity = 128;

        DTERR_C(dtiox_zephyr_modbus_slave_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.iox_handle = iox_handle;
        c.node_name = "dtmc_zephyr/demo_modbus/slave";
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
    dtiox_dispose(iox_handle);

    return rc;
}
