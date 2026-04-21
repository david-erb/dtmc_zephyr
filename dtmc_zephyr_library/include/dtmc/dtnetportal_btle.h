/*
 * dtnetportal_btle -- Zephyr Bluetooth LE GATT backend for the dtnetportal interface.
 *
 * Implements the dtnetportal vtable over the Zephyr BLE stack, advertising
 * under a configurable device name and exchanging topic payloads via GATT
 * characteristics. A new-buffer callback delivers inbound data to the
 * application, and a GATT read callback services outbound reads from a
 * connected central. An optional advertising tasker manages the BLE
 * advertisement loop as a named Zephyr thread.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtnetportal.h>

#define DTNETPORTAL_BTLE_FLAVOR "dtnetportal_btle"
#define DTNETPORTAL_BTLE_VERSION "1.0.0"

// callback function type for new buffer notifications
typedef dterr_t* (*dtnetportal_btle_newbuffer_callback_t)(void* recipient_self, dtbuffer_t* buffer);

typedef struct dtnetportal_btle_config_t
{
    const char* device_name; // name of the device to advertise
} dtnetportal_btle_config_t;

typedef struct dtnetportal_btle_t dtnetportal_btle_t;

extern dterr_t*
dtnetportal_btle_create(dtnetportal_btle_t** self_ptr);

extern dterr_t*
dtnetportal_btle_init(dtnetportal_btle_t* self);

extern dterr_t*
dtnetportal_btle_configure(dtnetportal_btle_t* self, dtnetportal_btle_config_t* config);

extern dterr_t*
dtnetportal_btle_activate(dtnetportal_btle_t* self);

extern dterr_t*
dtnetportal_btle_start_advertising_tasker(dtnetportal_btle_t* self);

extern void
dtnetportal_btle_dispose(dtnetportal_btle_t* self);

// -------------------------------------------------------------------------------
// called when connected central device issues a GATT Read request
typedef dterr_t*
dtnetportal_btle_read_callback_t(void* user_data, dtbuffer_t* buffer);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTNETPORTAL_DECLARE_API(dtnetportal_btle);
DTOBJECT_DECLARE_API(dtnetportal_btle);