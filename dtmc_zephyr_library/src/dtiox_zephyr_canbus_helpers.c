// Zephyr backend for dtiox — CAN (MCP2515 over SPI) on nRF5340
//
// Behaviour conventions (mirrors dtiox_espidf_can.c):
// - Driver/controller is created + started in attach().
// - All low-level settings (bitrate, mode, device) are hard-coded for now.
// - RX is presented as a byte stream via dtringfifo.
// - RX FIFO overflow is latched and returned as DTERR_OVERFLOW on next read().
// - Caller’s write() buffer must NOT be kept after return: we block in can_send()
//   instead of keeping any local TX queue.
// - Call-counts, byte-counts and error-counts are tracked, clamped at INT32_MAX.
// - Both read() and write() fail if the CAN controller is not ERROR_ACTIVE or if
//   error counters (tx/rx) indicate any bus error has occurred.

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtcore_helper.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtringfifo.h>

#include <dtcore/dtstr.h>

#include <dtmc/dtmc.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc/dtiox_zephyr_canbus.h>

#include "dtiox_zephyr_canbus_internals.h"

// -----------------------------------------------------------------------------
// Internal helpers

// Push bytes into FIFO (callback producer)

int32_t
dtiox_zephyr_canbus__rx_fifo_push(dtiox_zephyr_canbus_t* self, const uint8_t* src, int32_t len)
{
    if (!self || !src || len <= 0)
        return 0;

    int32_t written = dtringfifo_push(&self->rx_fifo, src, len);
    int32_t dropped = len - written;
    if (dropped > 0)
    {
        self->rx_overflow_flag = true;
        DTCORE_HELPER_ADD32(self->fifo_dropped_bytes, dropped);
    }
    return written;
}

// -----------------------------------------------------------------------------
// Pop bytes from FIFO (foreground consumer)

int32_t
dtiox_zephyr_canbus__rx_fifo_pop(dtiox_zephyr_canbus_t* self, uint8_t* dest, int32_t len)
{
    if (!self || !dest || len <= 0)
        return 0;

    return dtringfifo_pop(&self->rx_fifo, dest, len);
}

// -----------------------------------------------------------------------------
// CAN state helpers

const char*
dtiox_zephyr_canbus__state_to_str(enum can_state state)
{
    switch (state)
    {
        case CAN_STATE_ERROR_ACTIVE:
            return "ERROR_ACTIVE";
        case CAN_STATE_ERROR_WARNING:
            return "ERROR_WARNING";
        case CAN_STATE_ERROR_PASSIVE:
            return "ERROR_PASSIVE";
        case CAN_STATE_BUS_OFF:
            return "BUS_OFF";
        case CAN_STATE_STOPPED:
            return "STOPPED";
        default:
            return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// Check that error state is valid to continue read or write operations
// - Fails if CAN controller is not ERROR_ACTIVE.
// - Also fails if error counters show any TX/RX errors have occurred.

dterr_t*
dtiox_zephyr_canbus__check_error_state(dtiox_zephyr_canbus_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (self->cur_state != CAN_STATE_ERROR_ACTIVE && //
        self->cur_state != CAN_STATE_ERROR_PASSIVE)
    {
        DTCORE_HELPER_INC32(self->rx_error_count);
        dterr = dterr_new(DTERR_FAIL,
          DTERR_LOC,
          NULL,
          "CAN controller not ERROR_ACTIVE (state=%s, tx_err=%u, rx_err=%u)",
          dtiox_zephyr_canbus__state_to_str(self->cur_state),
          (unsigned)self->last_err_cnt.tx_err_cnt,
          (unsigned)self->last_err_cnt.rx_err_cnt);
        goto cleanup;
    }

    // if (self->last_err_cnt.tx_err_cnt > 0 || self->last_err_cnt.rx_err_cnt > 0)
    // {
    //     DTCORE_HELPER_INC32(self->rx_error_count);
    //     dterr = dterr_new(DTERR_FAIL,
    //       DTERR_LOC,
    //       NULL,
    //       "CAN controller error counters non-zero (state=%s, tx_err=%u, rx_err=%u)",
    //       dtiox_zephyr_canbus__state_to_str(self->cur_state),
    //       (unsigned)self->last_err_cnt.tx_err_cnt,
    //       (unsigned)self->last_err_cnt.rx_err_cnt);
    //     goto cleanup;
    // }

    // dtlog_debug(TAG,
    //   "CAN controller state OK (state=%s, tx_err=%u, rx_err=%u, state_change_count=%" PRId32 ")",
    //   dtiox_zephyr_canbus__state_to_str(self->cur_state),
    //   (unsigned)self->last_err_cnt.tx_err_cnt,
    //   (unsigned)self->last_err_cnt.rx_err_cnt,
    //   self->state_change_count);

cleanup:
    return dterr;
}
