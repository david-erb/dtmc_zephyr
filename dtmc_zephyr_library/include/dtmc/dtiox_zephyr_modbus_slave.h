/*
 * dtiox_zephyr_modbus_slave -- Zephyr Modbus RTU slave backend for the dtiox interface.
 *
 * Implements the dtiox vtable using the Zephyr Modbus subsystem in slave
 * mode, identified by a devicetree interface name (e.g. "MODBUS"). Incoming
 * Modbus register traffic is surfaced as a byte stream through the standard
 * dtiox read path; unit ID, UART framing parameters, and RX ring buffer
 * capacity are all set at configuration time without exposing Zephyr Modbus
 * API types.
 *
 * cdox v1.0.2
 */
#pragma once

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtiox.h>

#include <dtmc_base/dtuart_helpers.h>

// forward-declare concrete type
typedef struct dtiox_zephyr_modbus_slave_t dtiox_zephyr_modbus_slave_t;

// this is the public configuration structure, uses generic data types only
typedef struct
{
    // Modbus interface name as defined by Zephyr's Modbus subsystem / devicetree.
    // Example from Zephyr samples: "MODBUS" or "modbus0" depending on board overlay.
    const char* device_tree_name;

    // Modbus RTU unit ID (slave address). Typical range: 1..247.
    int32_t unit_id;

    dtuart_helper_config_t uart_config;

    // RX ring buffer capacity in bytes (incoming PUT_BLOB payloads).
    int32_t rx_ring_capacity;

} dtiox_zephyr_modbus_slave_config_t;

extern dterr_t*
dtiox_zephyr_modbus_slave_create(dtiox_zephyr_modbus_slave_t** self_ptr);
extern dterr_t*
dtiox_zephyr_modbus_slave_init(dtiox_zephyr_modbus_slave_t* self);
extern dterr_t*
dtiox_zephyr_modbus_slave_configure(dtiox_zephyr_modbus_slave_t* self, const dtiox_zephyr_modbus_slave_config_t* cfg);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTIOX_DECLARE_API(dtiox_zephyr_modbus_slave);
DTOBJECT_DECLARE_API(dtiox_zephyr_modbus_slave);
