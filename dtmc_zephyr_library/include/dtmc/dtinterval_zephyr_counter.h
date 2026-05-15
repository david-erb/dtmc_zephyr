/*
 * dtinterval_zephyr_counter -- Zephyr counter-driver backend for the dtinterval interface.
 *
 * Implements the dtinterval vtable using a hardware counter peripheral (e.g. TIMER1
 * on nRF5340) via the Zephyr counter driver API. Fires a channel alarm for each tick,
 * re-arming at an absolute timestamp to eliminate cumulative drift.
 *
 * Resolution is determined by the counter's hardware frequency (typically 1MHz for
 * nRF TIMER peripherals), giving microsecond-accurate intervals independent of the
 * system tick rate. Using a TIMER peripheral requires HFXO to remain active; for
 * sleep-compatible intervals at lower resolution use dtinterval_zephyr_ktimer instead.
 *
 * Requires CONFIG_COUNTER=y and a counter device node enabled in the devicetree.
 */
#pragma once

#include <stdbool.h>

#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtinterval.h>

typedef struct dtinterval_zephyr_counter_config_t
{
    const char*          name;
    int32_t              periodic_interval_micros;
    const struct device* counter_dev; // e.g. DEVICE_DT_GET(DT_NODELABEL(timer1))
} dtinterval_zephyr_counter_config_t;

typedef struct dtinterval_zephyr_counter_t dtinterval_zephyr_counter_t;

extern dterr_t*
dtinterval_zephyr_counter_register_vtables(void);

extern dterr_t*
dtinterval_zephyr_counter_create(dtinterval_zephyr_counter_t** self_ptr);

extern dterr_t*
dtinterval_zephyr_counter_init(dtinterval_zephyr_counter_t* self);

extern dterr_t*
dtinterval_zephyr_counter_configure(dtinterval_zephyr_counter_t* self,
                                    const dtinterval_zephyr_counter_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTINTERVAL_DECLARE_API(dtinterval_zephyr_counter);
