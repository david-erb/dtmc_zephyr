/*
 * dtiox_zephyr_uartirq -- Zephyr interrupt-driven UART backend for the dtiox interface.
 *
 * Implements the dtiox vtable using the Zephyr UART interrupt API, identified
 * by a devicetree node name (e.g. "uart0"). Baud rate, parity, data bits,
 * stop bits, and flow control are applied through the shared
 * dtuart_helper_config_t. Separate RX and TX ring buffer capacities are
 * configurable, decoupling interrupt latency from application read timing.
 *
 * cdox v1.0.2
 */
#pragma once

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtuart_helpers.h>

// forward-declare concrete type
typedef struct dtiox_zephyr_uartirq_t dtiox_zephyr_uartirq_t;

// this is the public configuration structure, uses generic data types only
typedef struct
{
    const char* device_tree_name; // e.g., "uart0"

    dtuart_helper_config_t uart_config;

    // RX ring buffer capacity in bytes.
    int32_t rx_capacity;

    // TX ring buffer capacity in bytes.
    int32_t tx_capacity;

} dtiox_zephyr_uartirq_config_t;

extern dterr_t*
dtiox_zephyr_uartirq_create(dtiox_zephyr_uartirq_t** self_ptr);
extern dterr_t*
dtiox_zephyr_uartirq_init(dtiox_zephyr_uartirq_t* self);
extern dterr_t*
dtiox_zephyr_uartirq_configure(dtiox_zephyr_uartirq_t* self, const dtiox_zephyr_uartirq_config_t* cfg);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTIOX_DECLARE_API(dtiox_zephyr_uartirq);
DTOBJECT_DECLARE_API(dtiox_zephyr_uartirq);