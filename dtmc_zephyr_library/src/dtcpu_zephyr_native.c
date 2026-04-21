#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtcpu.h>

// Q32.32 scale: us_per_cycle = (1e6 / hw_cycles_per_sec)
static uint64_t g_us_per_cycle_q32;

// -----------------------------------------------------------------------------
dterr_t*
dtcpu_sysinit(void)
{
    uint32_t hz = sys_clock_hw_cycles_per_sec();
    g_us_per_cycle_q32 = ((uint64_t)1000000ULL << 32) / (uint64_t)hz;
    return NULL;
}

// -----------------------------------------------------------------------------
void
dtcpu_mark(dtcpu_t* m)
{
    m->old = m->new;
    m->new = k_cycle_get_32();
}

// -----------------------------------------------------------------------------
uint64_t
dtcpu_elapsed_cycles(const dtcpu_t* m)
{
    return (uint32_t)(m->new - m->old);
}

// -----------------------------------------------------------------------------
uint64_t
dtcpu_elapsed_microseconds(const dtcpu_t* m)
{
    uint64_t cycles = (uint32_t)(m->new - m->old);
    return (cycles * g_us_per_cycle_q32) >> 32;
}

// -----------------------------------------------------------------------------
void
dtcpu_busywait_microseconds(uint64_t us)
{
    // k_busy_wait takes uint32_t microseconds; loop for large values
    while (us > UINT32_MAX)
    {
        k_busy_wait(UINT32_MAX);
        us -= UINT32_MAX;
    }
    if (us > 0)
    {
        k_busy_wait((uint32_t)us);
    }
}

// -----------------------------------------------------------------------------
int32_t
dtcpu_random_int32(void)
{
    return (int32_t)sys_rand32_get();
}
