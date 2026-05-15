/*
 * dtinterval_zephyr_ktimer -- Zephyr k_timer backend for the dtinterval periodic timer interface.
 *
 * Implements the dtinterval vtable using a hardware-backed k_timer. Register a
 * callback via dtinterval_zephyr_ktimer_set_callback() before calling start(); start()
 * blocks and dispatches one tick at a time to the callback, keeping the sample
 * period accurate regardless of how long each callback takes. Call pause() to
 * stop the timer and unblock start().
 *
 * Timer accuracy is limited to one system tick (CONFIG_SYS_CLOCK_TICKS_PER_SEC).
 * For sub-millisecond accuracy use dtinterval_zephyr_counter instead.
 */
#pragma once

#include <stdbool.h>

#include <zephyr/kernel.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtinterval.h>

typedef struct dtinterval_zephyr_ktimer_config_t
{
    const char* name;
    int32_t     periodic_interval_micros;
} dtinterval_zephyr_ktimer_config_t;

typedef struct dtinterval_zephyr_ktimer_t dtinterval_zephyr_ktimer_t;

extern dterr_t*
dtinterval_zephyr_ktimer_register_vtables(void);

extern dterr_t*
dtinterval_zephyr_ktimer_create(dtinterval_zephyr_ktimer_t** self_ptr);

extern dterr_t*
dtinterval_zephyr_ktimer_init(dtinterval_zephyr_ktimer_t* self);

extern dterr_t*
dtinterval_zephyr_ktimer_configure(dtinterval_zephyr_ktimer_t* self, const dtinterval_zephyr_ktimer_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTINTERVAL_DECLARE_API(dtinterval_zephyr_ktimer);
