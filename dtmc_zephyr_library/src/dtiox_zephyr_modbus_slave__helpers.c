#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtmodbus_helpers.h>

#include "dtiox_zephyr_modbus_slave__internals.h"

#define TAG "dtiox_zephyr_modbus_slave"

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave__release_lock(dtiox_zephyr_modbus_slave_t* self, dterr_t* dterr_in)
{
    if (self == NULL || self->lock_handle == NULL)
        return dterr_in;

    dterr_t* dterr = dtlock_release(self->lock_handle);

    // let incoming error take precedence
    if (dterr_in != NULL)
    {
        dterr_dispose(dterr);
        return dterr_in;
    }
    return dterr;
}

// -----------------------------------------------------------------------------
// Consume PUT_BLOB payload that has been written into the M2S shadow regs.
// Caller must hold self->mutex.

dterr_t*
dtiox_zephyr_modbus_slave__put_m2s_into_rxfifo(dtiox_zephyr_modbus_slave_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    int32_t byte_len = (int32_t)self->m2s_len;
    if (byte_len < 0)
        byte_len = 0;
    if (byte_len > DTIOX_MODBUS_MAX_BLOB_BYTES)
        byte_len = DTIOX_MODBUS_MAX_BLOB_BYTES;

    // Unpack regs -> bytes.
    uint8_t tmp[DTIOX_MODBUS_MAX_BLOB_BYTES];
    dtmodbus_helpers_unpack_regs_to_bytes(self->m2s_data_regs, byte_len, tmp);

    // Push into RX FIFO (application reads via dtiox_read()).
    int32_t written = dtringfifo_push(&self->rx_fifo, tmp, byte_len);
    if (written < byte_len)
    {
        self->rx_overflow_pending = true;
        self->stats.rx_dropped += (byte_len - written);
    }

    if (self->rx_semaphore != NULL && written > 0)
        dtsemaphore_post(self->rx_semaphore);

    self->stats.put_blob_count++;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Prepare S2M response registers for the next master read after GIVE_ME_ANY_DATA.
// Caller must hold self->mutex.

dterr_t*
dtiox_zephyr_modbus_slave__put_tx_staged_into_s2m(dtiox_zephyr_modbus_slave_t* self)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);

    if (self->tx_staged_pending && self->tx_staged_len > 0)
    {
        int32_t byte_len = self->tx_staged_len;
        if (byte_len > DTIOX_MODBUS_MAX_BLOB_BYTES)
            byte_len = DTIOX_MODBUS_MAX_BLOB_BYTES;

        self->s2m_status = (uint16_t)DTIOX_MODBUS_STATUS_HAS_DATA;
        self->s2m_len = (uint16_t)byte_len;

        dtmodbus_helpers_pack_bytes_to_regs((const uint8_t*)self->tx_staged_buf->payload, byte_len, self->s2m_data_regs);

        // Consume staged blob: only one outstanding message at a time.
        self->tx_staged_pending = false;
        self->tx_staged_len = 0;
    }
    else
    {
        self->s2m_status = (uint16_t)DTIOX_MODBUS_STATUS_NO_DATA;
        self->s2m_len = 0;
        memset(self->s2m_data_regs, 0, sizeof(self->s2m_data_regs));
    }

    self->stats.give_me_any_data_count++;

cleanup:
    return dterr;
}
