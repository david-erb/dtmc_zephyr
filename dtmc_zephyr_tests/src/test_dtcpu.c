#include <inttypes.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtcore/dtunittest.h>

#include <dtmc_base/dtcpu.h>

#define TAG "test_dtmc_zephyr_dtcpu"

// ----------------------------------------------------------------------------------
// Unit test: Microsecond time should measure elapsed time accurately
static dterr_t*
test_dtmc_zephyr_dtcpu_elapsed_microseconds()
{
    dterr_t* dterr = NULL;

    DTERR_C(dtcpu_sysinit());

    int micros = 100000;

    dtcpu_t cpu = { 0 };
    dtcpu_mark(&cpu);
    // need busy wait here because timer stops during sleep waits
    k_busy_wait(micros);
    dtcpu_mark(&cpu);
    uint64_t elapsed = dtcpu_elapsed_microseconds(&cpu);

    DTUNITTEST_ASSERT_UINT64(elapsed, >=, 95000);
    DTUNITTEST_ASSERT_UINT64(elapsed, <=, 105000);

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
void
test_dtmc_zephyr_dtcpu(DTUNITTEST_SUITE_ARGS)
{
    DTUNITTEST_RUN_TEST(test_dtmc_zephyr_dtcpu_elapsed_microseconds);
}
