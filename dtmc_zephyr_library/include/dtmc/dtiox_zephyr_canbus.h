/*
 * dtiox_zephyr_canbus -- Zephyr CAN backend for the dtiox byte-stream interface.
 *
 * Implements the dtiox vtable over the Zephyr CAN subsystem, presenting bus
 * traffic as a plain byte stream. A configurable TX identifier and an RX ring
 * buffer isolate the application from frame boundaries. Both standard 11-bit
 * and extended 29-bit identifiers are supported via the use_extended_id flag.
 *
 * cdox v1.0.2
 */
#pragma once

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtiox.h>

// Forward-declare concrete type
typedef struct dtiox_zephyr_canbus_t dtiox_zephyr_canbus_t;

typedef struct
{
    uint32_t txid;
    bool use_extended_id;

    // RX ring buffer capacity in bytes.
    // 0 => backend default (currently 1024 bytes).
    int32_t rx_ring_capacity;

} dtiox_zephyr_canbus_config_t;

extern dterr_t*
dtiox_zephyr_canbus_create(dtiox_zephyr_canbus_t** self_ptr);

extern dterr_t*
dtiox_zephyr_canbus_init(dtiox_zephyr_canbus_t* self);

extern dterr_t*
dtiox_zephyr_canbus_configure(dtiox_zephyr_canbus_t* self, const dtiox_zephyr_canbus_config_t* config);

// -----------------------------------------------------------------------------
// Interface plumbing (dtiox facade implementation).

DTIOX_DECLARE_API(dtiox_zephyr_canbus);
DTOBJECT_DECLARE_API(dtiox_zephyr_canbus);