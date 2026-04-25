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

#include <dtcore/dtcore_helper.h>
#include <dtcore/dtstr.h>

#include <dtmc/dtmc.h>
#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc/dtiox_zephyr_canbus.h>

#include "dtiox_zephyr_canbus_internals.h"

// -----------------------------------------------------------------------------
// CAN RX callback
//
// We always keep the filter installed, but gate FIFO writes via rx_enabled.

void
dtiox_zephyr_canbus__rx_callback(const struct device* dev, struct can_frame* frame, void* user_data)
{
    (void)dev;
    dtiox_zephyr_canbus_t* self = (dtiox_zephyr_canbus_t*)user_data;
    if (!self)
        return;

    DTCORE_HELPER_INC32(self->stats.rx_callback_calls);

    if (!frame)
        return;

    uint8_t len = can_dlc_to_bytes(frame->dlc);
    if (len > 8)
        len = 8;

    DTCORE_HELPER_ADD32(self->stats.rx_callback_bytes, len);

    if (self->rx_enabled && len > 0)
    {
        (void)dtiox_zephyr_canbus__rx_fifo_push(self, frame->data, (int32_t)len);
        DTCORE_HELPER_ADD32(self->stats.rx_bytes, len);

        // signal user code that data is available (needs to be isr safe!)
        dtsemaphore_post(self->rx_semaphore);
    }
}

// -----------------------------------------------------------------------------
// CAN TX callback

void
dtiox_zephyr_canbus__tx_callback(const struct device* dev, int error, void* user_data)
{
    (void)dev;
    dtiox_zephyr_canbus_t* self = (dtiox_zephyr_canbus_t*)user_data;
    if (!self)
        return;

    if (error != 0)
    {
        DTCORE_HELPER_INC32(self->tx_error_count);
    }

    self->last_tx_error = error;
}

// -----------------------------------------------------------------------------
// CAN state change callback, called in interrupt context

void
dtiox_zephyr_canbus__state_change_callback( //
  const struct device* dev,                 //
  enum can_state state,                     //
  struct can_bus_err_cnt err_cnt,           //
  void* user_data)
{
    (void)dev;
    dtiox_zephyr_canbus_t* self = (dtiox_zephyr_canbus_t*)user_data;
    if (!self)
        return;

    DTCORE_HELPER_INC32(self->state_change_count);

    self->cur_state = state;
    self->last_err_cnt = err_cnt;
}
