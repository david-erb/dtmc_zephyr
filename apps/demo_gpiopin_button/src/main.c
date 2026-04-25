#include <dtcore/dterr.h>

#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtgpiopin.h>
#include <dtmc_base_demos/demo_gpiopin_button.h>

#include <dtmc/dtgpiopin_zephyr.h>

#include <dtmc_base/dtcpu.h>

#define TAG "main"

// --------------------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    dterr_t* dterr = NULL;
    dtgpiopin_handle gpiopin_handle = NULL;
    demo_t* demo = NULL;

    // === create the concrete objects we need ===
    {
        dtgpiopin_zephyr_t* o = NULL;
        DTERR_C(dtgpiopin_zephyr_create(&o));
        gpiopin_handle = (dtgpiopin_handle)o;

        dtgpiopin_zephyr_config_t c = { 0 };
        c.pin_number = 32; // P1.0 on nRF5340-DK
        c.mode = DTGPIOPIN_MODE_INPUT;
        c.pull = DTGPIOPIN_PULL_UP;
        DTERR_C(dtgpiopin_zephyr_configure(o, &c));
    }

    // === create and configure the demo instance ===
    {
        DTERR_C(demo_create(&demo));
        demo_config_t c = { 0 };
        c.gpiopin_handle = gpiopin_handle;
        DTERR_C(demo_configure(demo, &c));
    }

    // === start the demo ===
    DTERR_C(demo_start(demo));

cleanup:
    // log and dispose error chain (if any)
    dtlog_dterr(TAG, dterr);
    dterr_dispose(dterr);

    // dispose because start has left things running
    demo_dispose(demo);

    // dispose the GPIO pin instance
    dtgpiopin_dispose(gpiopin_handle);

    return 0;
}
