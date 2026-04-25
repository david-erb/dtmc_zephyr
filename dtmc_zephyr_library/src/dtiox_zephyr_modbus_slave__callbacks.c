#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>

#include <dtcore/dtlog.h>

#include "dtiox_zephyr_modbus_slave__internals.h"

#define TAG "dtiox_zephyr_modbus_slave"

// Zephyr's modbus user callbacks do not provide a user-data pointer.
// This backend is therefore implemented as a single-instance module.
static dtiox_zephyr_modbus_slave_t* g_self = NULL;

void
dtiox_zephyr_modbus_slave__callbacks_set_self(dtiox_zephyr_modbus_slave_t* self)
{
    g_self = self;
}

// ------------------------------------------------------------------------------
// here we are responding to holding register read requests from the modbus master
// we just transfer whatever we have stored in our S2M shadow registers
// the S2M shadow registers are populated during dtiox_zephyr_modbus_slave__put_tx_staged_into_s2m
// that gets called as a result of the master writing the GIVE_ME_ANY_DATA command into the M2S registers
static int
holding_reg_rd(uint16_t addr, uint16_t* out_reg)
{
    if (g_self == NULL || out_reg == NULL)
        return -EIO;

    if (!g_self->_is_callback_processing_enabled)
        return 0;

    // Only S2M registers are exposed for reads in this protocol.
    if (addr == DTIOX_MODBUS_REG_S2M_STATUS)
    {
        *out_reg = g_self->s2m_status;
        g_self->stats.s2m_transfers_started_count++;
        return 0;
    }
    if (addr == DTIOX_MODBUS_REG_S2M_STATUS + 1)
    {
        *out_reg = g_self->s2m_len;
        return 0;
    }

    if (addr >= DTIOX_MODBUS_REG_S2M_DATA && addr < (DTIOX_MODBUS_REG_S2M_DATA + DTIOX_MODBUS_MAX_BLOB_REGS))
    {
        uint16_t i = (uint16_t)(addr - DTIOX_MODBUS_REG_S2M_DATA);
        *out_reg = g_self->s2m_data_regs[i];
        return 0;
    }

    // Anything else: return 0 to behave predictably rather than erroring.
    *out_reg = 0;
    return 0;
}

// ------------------------------------------------------------------------------
// here the master is sending us stuff
static int
holding_reg_wr(uint16_t addr, uint16_t reg)
{
    if (g_self == NULL)
        return -EIO;

    if (!g_self->_is_callback_processing_enabled)
        return 0;

    // Protocol is defined over the M2S register region.
    if (addr == DTIOX_MODBUS_REG_M2S_CMD)
    {
        // New command starts a new transaction.
        g_self->m2s_cmd = reg;
        g_self->m2s_len = 0;
        g_self->m2s_data_regs_expected = 0;
        g_self->m2s_data_regs_written = 0;
        memset(g_self->m2s_data_regs, 0, sizeof(g_self->m2s_data_regs));
        return 0;
    }

    // command's length is received next
    if (addr == DTIOX_MODBUS_REG_M2S_CMD + 1)
    {
        g_self->m2s_len = reg;

        int32_t len = (int32_t)g_self->m2s_len;
        if (len < 0)
            len = 0;
        if (len > DTIOX_MODBUS_MAX_BLOB_BYTES)
            len = DTIOX_MODBUS_MAX_BLOB_BYTES;

        g_self->m2s_data_regs_expected = DTIOX_MODBUS_BLOB_TO_REGS(len);
        if (g_self->m2s_data_regs_expected > DTIOX_MODBUS_MAX_BLOB_REGS)
            g_self->m2s_data_regs_expected = DTIOX_MODBUS_MAX_BLOB_REGS;

        // Commands with no payload can be processed immediately after header.
        if (g_self->m2s_cmd == (uint16_t)DTIOX_MODBUS_CMD_GIVE_ME_ANY_DATA)
        {

            g_self->stats.m2s_polls_started_count++;
            dtlock_acquire(g_self->lock_handle);
            (void)dtiox_zephyr_modbus_slave__put_tx_staged_into_s2m(g_self);
            dtlock_release(g_self->lock_handle);
        }
        else
        {
            // For PUT_BLOB, we wait for the payload registers to be written.
            g_self->stats.m2s_blobs_started_count++;
        }

        return 0;
    }

    // master is writing a blob?
    if (addr >= DTIOX_MODBUS_REG_M2S_DATA && addr < (DTIOX_MODBUS_REG_M2S_DATA + DTIOX_MODBUS_MAX_BLOB_REGS))
    {
        uint16_t i = (uint16_t)(addr - DTIOX_MODBUS_REG_M2S_DATA);
        if (i < DTIOX_MODBUS_MAX_BLOB_REGS)
        {
            g_self->m2s_data_regs[i] = reg;
            g_self->m2s_data_regs_written++;

            if (g_self->m2s_cmd == (uint16_t)DTIOX_MODBUS_CMD_PUT_BLOB)
            {
                // When we've received the full payload, consume it.
                if (g_self->m2s_data_regs_expected > 0 && g_self->m2s_data_regs_written >= g_self->m2s_data_regs_expected)
                {
                    dtlock_acquire(g_self->lock_handle);
                    (void)dtiox_zephyr_modbus_slave__put_m2s_into_rxfifo(g_self);
                    dtlock_release(g_self->lock_handle);
                }
            }
        }
        return 0;
    }

    // Ignore anything else.
    return 0;
}

// Exported callback table for modbus server.
struct modbus_user_callbacks dtiox_zephyr_modbus_slave__user_cbs = {
    .coil_rd = NULL,
    .coil_wr = NULL,
    .discrete_input_rd = NULL,
    .input_reg_rd = NULL,
    .holding_reg_rd = holding_reg_rd,
    .holding_reg_wr = holding_reg_wr,
};
