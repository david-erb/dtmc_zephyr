// dtcpu_zephyr.c — simple & fast

#include <stdbool.h>
#include <stdint.h>

#include <cmsis_core.h> // CoreDebug, DWT
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtcpu.h>

// Q32.32 scale: us_per_cycle = (1e6 / SystemCoreClock)
static uint64_t g_us_per_cycle_q32;

// -----------------------------------------------------------------------------
static inline bool
dwt_supported(void)
{
    return (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0U;
}

// -----------------------------------------------------------------------------
static inline void
dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
}

// -----------------------------------------------------------------------------
dterr_t*
dtcpu_sysinit(void)
{
    if (!dwt_supported())
    {
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "DWT CYCCNT not supported");
    }
    dwt_enable();
    // Precompute reciprocal (no divides in the hot path)
    g_us_per_cycle_q32 = ((uint64_t)1000000ULL << 32) / (uint64_t)SystemCoreClock;

    return NULL;
}

// -----------------------------------------------------------------------------
void
dtcpu_mark(dtcpu_t* m)
{
    // caller promises: called once before and once after the hot section
    m->old = m->new;
    m->new = (uint32_t)DWT->CYCCNT;
}

// -----------------------------------------------------------------------------
uint64_t
dtcpu_elapsed_cycles(const dtcpu_t* m)
{
    // Unsigned subtraction handles single wrap transparently
    return (uint32_t)(m->new - m->old);
}

// -----------------------------------------------------------------------------
uint64_t
dtcpu_elapsed_microseconds(const dtcpu_t* m)
{
    // Q32.32: us = (cycles * us_per_cycle_q32) >> 32
    uint64_t cycles = (uint32_t)(m->new - m->old);
    return (cycles * g_us_per_cycle_q32) >> 32;
}

// -----------------------------------------------------------------------------
// this doesn't work unless you call dtcpu_sysinit() first to set up the DWT and compute the reciprocal
void
dtcpu_busywait_microseconds(uint64_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (uint32_t)((us * (uint64_t)SystemCoreClock) / 1000000ULL);

    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
        __NOP();
    }
}

// -----------------------------------------------------------------------------
int32_t
dtcpu_random_int32(void)
{
    return (int32_t)sys_rand32_get();
}