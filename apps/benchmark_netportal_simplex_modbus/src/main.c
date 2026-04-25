#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtruntime.h>

#include <dtmc_base/dtframer.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtsemaphore.h>

// we can cover these concrete objects platform agnostically
#include <dtmc_base/dtframer_simple.h>
#include <dtmc_base/dtnetportal_iox.h>
#include <dtmc_base/dtsemaphore.h>

// these concrete objects are platform specific
#include <dtmc/dtiox_zephyr_modbus_slave.h>

#include <dtmc_base_benchmarks/benchmark_netportal_simplex.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtiox_handle iox_handle = NULL;
    dtframer_handle framer_handle = NULL;
    dtnetportal_handle netportal_handle = NULL;

    benchmark_t* benchmark = NULL;

    // ==== print the currently registered devices ====
    {
        char* s = NULL;
        DTERR_C(dtruntime_format_devices_as_table(&s));

        dtlog_info(TAG, "devices:\n%s", s);

        dtstr_dispose(s);
    }

    // === create the concrete IOX object we need ===
    {
        dtiox_zephyr_modbus_slave_t* o = NULL;
        DTERR_C(dtiox_zephyr_modbus_slave_create(&o));
        iox_handle = (dtiox_handle)o;
        dtiox_zephyr_modbus_slave_config_t c = { 0 };
        c.device_tree_name = "benchmark_modbus";
        c.unit_id = 1;
        c.uart_config = dtuart_helper_default_config;
        // enough to absorb a single master write with all the modbus registers in our protocol
        c.rx_ring_capacity = 256;

        DTERR_C(dtiox_zephyr_modbus_slave_configure(o, &c));
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

    // === create and configure the benchmark instance ===
    {
        DTERR_C(benchmark_create(&benchmark));
        benchmark_config_t c = { 0 };
        c.netportal_handle = netportal_handle;
        // zephyr modbus slave is finicky about writing to a non-existent master
        c.is_server = true;
        DTERR_C(benchmark_configure(benchmark, &c));
    }

    // === start the benchmark ===
    DTERR_C(benchmark_start(benchmark));

cleanup:
    int rc = (dterr != NULL) ? -1 : 0;

    // log and dispose error chain if any
    dtlog_dterr(TAG, dterr);

    dterr_dispose(dterr);

    // dispose the benchmark instance
    benchmark_dispose(benchmark);

    // dispose the objects

    dtnetportal_dispose(netportal_handle);

    dtframer_dispose(framer_handle);

    dtiox_dispose(iox_handle);

    return rc;
}
