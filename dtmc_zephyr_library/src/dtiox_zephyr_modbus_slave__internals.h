#pragma once
// Internal declarations for dtiox_zephyr_modbus_slave.

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dtcore_helper.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtringfifo.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox_modbus.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc_base/dtlock.h>

#include <dtmc/dtiox_zephyr_modbus_slave.h>

#define DTIOX_ZEPHYR_MODBUS_SLAVE_DEFAULT_UNIT_ID 1

typedef struct dtiox_zephyr_modbus_slave_stats_t
{
    int32_t m2s_polls_started_count;
    int32_t m2s_blobs_started_count;
    int32_t s2m_transfers_started_count;
    int32_t put_blob_count;
    int32_t give_me_any_data_count;
    int32_t rx_dropped;
    int32_t tx_dropped;
} dtiox_zephyr_modbus_slave_stats_t;

typedef struct dtiox_zephyr_modbus_slave_t
{
    DTIOX_COMMON_MEMBERS
    bool _is_malloced;
    bool _is_modbus_init_server;

    bool _is_callback_processing_enabled;

    dtiox_zephyr_modbus_slave_config_t config;

    dtsemaphore_handle rx_semaphore;

    // Modbus interface index for Zephyr modbus subsystem.
    int iface;

    // Incoming bytes from master PUT_BLOB.
    dtringfifo_t rx_fifo;
    dtbuffer_t* rx_fifo_buffer;
    bool rx_overflow_pending;

    // Outgoing bytes to master via GIVE_ME_ANY_DATA (single outstanding).
    dtbuffer_t* tx_staged_buf; // capacity == config.tx_capacity
    int32_t tx_staged_len;
    bool tx_staged_pending;

    // Shadow state for interpreting multi-register writes into the M2S region.
    uint16_t m2s_cmd;
    uint16_t m2s_len;
    uint16_t m2s_data_regs[DTIOX_MODBUS_MAX_BLOB_REGS];
    int32_t m2s_data_regs_expected;
    int32_t m2s_data_regs_written;

    // Registers exposed to master reads (S2M region).
    uint16_t s2m_status;
    uint16_t s2m_len;
    uint16_t s2m_data_regs[DTIOX_MODBUS_MAX_BLOB_REGS];

    dtlock_handle lock_handle;

    dtiox_zephyr_modbus_slave_stats_t stats;

} dtiox_zephyr_modbus_slave_t;

// helpers
extern dterr_t*
dtiox_zephyr_modbus_slave__release_lock(dtiox_zephyr_modbus_slave_t* self, dterr_t* dterr_in);

extern dterr_t*
dtiox_zephyr_modbus_slave__put_m2s_into_rxfifo(dtiox_zephyr_modbus_slave_t* self);

extern dterr_t*
dtiox_zephyr_modbus_slave__put_tx_staged_into_s2m(dtiox_zephyr_modbus_slave_t* self);

// modbus callbacks (single-instance binding)
extern void
dtiox_zephyr_modbus_slave__callbacks_set_self(dtiox_zephyr_modbus_slave_t* self);

extern struct modbus_user_callbacks dtiox_zephyr_modbus_slave__user_cbs;
