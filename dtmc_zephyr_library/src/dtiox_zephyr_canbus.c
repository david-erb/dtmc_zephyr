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

#include <dtcore/dtbuffer.h>
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

// vtable
DTIOX_INIT_VTABLE(dtiox_zephyr_canbus);
DTOBJECT_INIT_VTABLE(dtiox_zephyr_canbus);

// -----------------------------------------------------------------------------

dterr_t*
dtiox_zephyr_canbus_create(dtiox_zephyr_canbus_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    *self_ptr = (dtiox_zephyr_canbus_t*)malloc(sizeof(**self_ptr));
    if (!*self_ptr)
        return dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc %zu", sizeof(**self_ptr));

    DTERR_C(dtiox_zephyr_canbus_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr)
    {
        free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_zephyr_canbus_create failed");
    }
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtiox_zephyr_canbus_init(dtiox_zephyr_canbus_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_IOX_MODEL_ZEPHYR_CAN;

    DTERR_C(dtringfifo_init(&self->rx_fifo));

    self->can_dev = NULL;
    self->dev_ready = false;
    self->dev_started = false;
    self->cur_state = CAN_STATE_ERROR_ACTIVE;

    DTERR_C(dtiox_set_vtable(self->model_number, &dtiox_zephyr_canbus_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &_object_vt));

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_zephyr_canbus_init failed");
    return dterr;
}

// -----------------------------------------------------------------------------
// Configure: we still accept cfg, but hard-code low-level controller settings.
//
// cfg is mainly used for the CAN identifier / extended-ID flag and RX ring size.

dterr_t*
dtiox_zephyr_canbus_configure(dtiox_zephyr_canbus_t* self, const dtiox_zephyr_canbus_config_t* cfg)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(cfg);

    self->config = *cfg;

    self->tx_retry_milliseconds = 100;
    self->tx_retry_count = 5;

    if (self->config.rx_ring_capacity < 1)
    {
        dterr =
          dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "rx_ring_capacity must be > 0, got %" PRId32, self->config.rx_ring_capacity);
        goto cleanup;
    }

    // Allocate and configure RX FIFO storage
    {
        DTERR_C(dtbuffer_create(&self->rx_fifo_buffer, self->config.rx_ring_capacity));

        dtringfifo_config_t fifo_cfg = { 0 };
        fifo_cfg.buffer = self->rx_fifo_buffer->payload;
        fifo_cfg.capacity = self->rx_fifo_buffer->length;

        DTERR_C(dtringfifo_configure(&self->rx_fifo, &fifo_cfg));
    }
cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Attach:
// - Resolve device (hard-coded through DT_CHOSEN(zephyr,canbus)).
// - Allocate RX FIFO storage.
// - Configure bitrate/mode.
// - Start the CAN controller and install a catch-all RX filter.
//
// After attach(), the driver is started. enable() only controls whether
// RX bytes are pushed into the FIFO and resets counters.

dterr_t*
dtiox_zephyr_canbus_attach(dtiox_zephyr_canbus_t* self DTIOX_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    // Resolve CAN device
    self->can_dev = dtiox_zephyr_canbus_DEV;
    if (!device_is_ready(self->can_dev))
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "CAN device not ready");
        goto cleanup;
    }
    self->dev_ready = true;

    // hard-coded controller settings: bitrate + normal mode
    // DTERR_NEGERROR_C(can_set_bitrate(self->can_dev, dtiox_zephyr_canbus_BITRATE));
    // DTERR_NEGERROR_C(can_set_mode(self->can_dev, CAN_MODE_NORMAL));

    DTERR_NEGERROR_C(can_start(self->can_dev));
    self->dev_started = true;

    {
        const struct can_timing* timing;
        timing = can_get_timing_min(self->can_dev);
        dtlog_info(TAG,
          "CAN device started: timing min: sjw=%u prop_seg=%u phase_seg1=%u phase_seg2=%u prescaler=%u",
          timing->sjw,
          timing->prop_seg,
          timing->phase_seg1,
          timing->phase_seg2,
          timing->prescaler);
        timing = can_get_timing_max(self->can_dev);
        dtlog_info(TAG,
          "CAN device started: timing max: sjw=%u prop_seg=%u phase_seg1=%u phase_seg2=%u prescaler=%u",
          timing->sjw,
          timing->prop_seg,
          timing->phase_seg1,
          timing->phase_seg2,
          timing->prescaler);
    }
    // capture any new state changes from the callback
    can_set_state_change_callback(self->can_dev, dtiox_zephyr_canbus__state_change_callback, self);

    // get initial state
    DTERR_NEGERROR_C(can_get_state(self->can_dev, &self->cur_state, &self->last_err_cnt));

    // install catch-all RX filter (accept everything)
    {
        struct can_filter filter = (struct can_filter){
            .id = 0,
            .mask = 0, // 0 = accept all
            .flags = 0 // standard  frame only,
        };

        int filter_id = can_add_rx_filter(self->can_dev, dtiox_zephyr_canbus__rx_callback, self, &filter);
        if (filter_id < 0)
        {
            dterr = dterr_new(
              DTERR_FAIL, DTERR_LOC, NULL, "can_add_rx_filter failed: " DTERR_NEGERROR_FORMAT, DTERR_NEGERROR_ARGS(filter_id));
            goto cleanup;
        }
        self->rx_filter_id = filter_id;
        self->rx_filter_is_installed = true;
    }

    // Initial counters and flags
    self->rx_enabled = false;
    self->rx_overflow_flag = false;
    self->fifo_dropped_bytes = 0;
    self->rx_error_count = 0;

    memset(&self->stats, 0, sizeof(self->stats));

cleanup:
    if (dterr)
    {
        if (self->dev_started && self->can_dev)
        {
            (void)can_stop(self->can_dev);
            self->dev_started = false;
        }
    }
    return dterr;
}

// ----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_canbus_detach(dtiox_zephyr_canbus_t* self DTIOX_DETACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->rx_enabled = false;

    if (self->can_dev && self->rx_filter_is_installed)
    {
        can_remove_rx_filter(self->can_dev, self->rx_filter_id);
        self->rx_filter_is_installed = false;
    }

    if (self->can_dev && self->dev_started)
    {
        (void)can_stop(self->can_dev);
        self->dev_started = false;
    }

    dtringfifo_reset(&self->rx_fifo);

    self->fifo_dropped_bytes = 0;
    self->rx_overflow_flag = false;

    self->stats.rx_bytes = 0;
    self->stats.rx_calls = 0;
    self->stats.tx_bytes = 0;
    self->stats.tx_calls = 0;

    self->rx_error_count = 0;
    self->tx_error_count = 0;
    self->last_tx_error = 0;

    memset(&self->last_err_cnt, 0, sizeof(self->last_err_cnt));
    self->state_change_count = 0;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Enable / disable
//
// Here, enable_rx_irq only controls whether RX callback pushes
// into FIFO and resets counters. The controller itself is already started
// in attach().

dterr_t*
dtiox_zephyr_canbus_enable(dtiox_zephyr_canbus_t* self DTIOX_ENABLE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (!self->dev_ready || !self->dev_started)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "CAN device not started");
        DTCORE_HELPER_INC32(self->rx_error_count);
        goto cleanup;
    }

    self->rx_enabled = enable_rx_irq;

    if (enable_rx_irq)
    {
        // Clear FIFO and counters each time we enable.
        dtringfifo_reset(&self->rx_fifo);

        self->rx_overflow_flag = false;
        self->fifo_dropped_bytes = 0;
        self->rx_error_count = 0;

        memset(&self->stats, 0, sizeof(self->stats));
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Non-blocking read from FIFO
//
// Semantics:
// - If an RX overflow has occurred earlier, the *next* read returns an error
//   and clears the overflow flag.
// - Otherwise, pop as many bytes as available up to buf_len, non-blocking.
// - Fails if controller is not ERROR_ACTIVE or has logged bus errors.

dterr_t*
dtiox_zephyr_canbus_read(dtiox_zephyr_canbus_t* self DTIOX_READ_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_read);

    *out_read = 0;

    if (!self->dev_ready || !self->dev_started)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "CAN device not started");
        DTCORE_HELPER_INC32(self->rx_error_count);
        goto cleanup;
    }

    DTERR_C(dtiox_zephyr_canbus__check_error_state(self));

    // Overflow is reported once on the next read.
    if (self->rx_overflow_flag)
    {
        self->rx_overflow_flag = false;
        DTCORE_HELPER_INC32(self->rx_error_count);
        dterr =
          dterr_new(DTERR_OVERFLOW, DTERR_LOC, NULL, "RX FIFO overflow (dropped %" PRId32 " bytes)", self->fifo_dropped_bytes);
        goto cleanup;
    }

    int32_t n = dtiox_zephyr_canbus__rx_fifo_pop(self, buf, buf_len);
    if (n < 0)
        n = 0;

    *out_read = n;
    DTCORE_HELPER_INC32(self->stats.rx_calls);
    DTCORE_HELPER_ADD32(self->stats.rx_bytes, n);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Write as a byte stream on top of CAN frames.
//
// Semantics:
// - Split buffer into up to 8-byte chunks and send frames sequentially.
// - Use can_send(..., K_FOREVER, ...) so we NEVER keep the caller's buffer
//   after this function returns. We block until the frame is queued/sent.
// - Fail if controller is not ERROR_ACTIVE or has logged bus errors.

dterr_t*
dtiox_zephyr_canbus_write(dtiox_zephyr_canbus_t* self DTIOX_WRITE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_written);

    *out_written = 0;

    if (!self->dev_ready || !self->dev_started)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "CAN device not started");
        DTCORE_HELPER_INC32(self->rx_error_count);
        goto cleanup;
    }

    int32_t total_written = 0;

    while (total_written < len)
    {
        int32_t chunk_len = len - total_written;
        if (chunk_len > 8)
            chunk_len = 8;

        struct can_frame frame;
        memset(&frame, 0, sizeof(frame));

        frame.id = self->config.txid;
        frame.dlc = can_bytes_to_dlc((uint8_t)chunk_len);
        frame.flags = 0;

        if (self->config.use_extended_id)
            frame.flags |= CAN_FRAME_IDE;

        memcpy(frame.data, buf + total_written, (size_t)chunk_len);

        int ret = 0;
        for (int32_t attempt = 0; attempt < self->tx_retry_count; ++attempt)
        {
            DTERR_C(dtiox_zephyr_canbus__check_error_state(self));

            ret = can_send(self->can_dev, &frame, K_MSEC(self->tx_retry_milliseconds), dtiox_zephyr_canbus__tx_callback, self);
            if (ret == 0)
            {
                break;
            }
            if (self->last_tx_error != 0)
            {
                dterr = dterr_new(
                  DTERR_FAIL, DTERR_LOC, NULL, "CAN transmit " DTERR_NEGERROR_FORMAT, DTERR_NEGERROR_ARGS(self->last_tx_error));
                goto cleanup;
            }
            if (ret == -EAGAIN)
            {
                continue;
            }
            if (ret < 0)
            {
                dterr =
                  dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "can_send failed: " DTERR_NEGERROR_FORMAT, DTERR_NEGERROR_ARGS(ret));
                goto cleanup;
            }
        }

        if (ret == -EAGAIN)
        {
            dterr = dterr_new(DTERR_TIMEOUT,
              DTERR_LOC,
              NULL,
              "can_send timeout after %" PRId32 " attempts of %" PRId32 " milliseconds, bus state is %s",
              self->tx_retry_count,
              self->tx_retry_milliseconds,
              dtiox_zephyr_canbus__state_to_str(self->cur_state));
            goto cleanup;
        }

        total_written += chunk_len;
        DTCORE_HELPER_ADD32(self->stats.tx_bytes, chunk_len);
    }

    *out_written = total_written;
    DTCORE_HELPER_INC32(self->stats.tx_calls);
cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Optional RX semaphore hook

dterr_t*
dtiox_zephyr_canbus_set_rx_semaphore(dtiox_zephyr_canbus_t* self DTIOX_SET_RX_SEMAPHORE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->rx_semaphore = rx_semaphore;
    // NOTE: Currently the RX callback does not post this semaphore.

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Human-readable status string

dterr_t*
dtiox_zephyr_canbus_concat_format(dtiox_zephyr_canbus_t* self DTIOX_CONCAT_FORMAT_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_str);

    const char* enabled_str = self->dev_started ? "started" : "stopped";

    *out_str = dtstr_concat_format(in_str,
      separator,
      "zephyr-can (%" PRId32 ") bitrate=%u id=0x%08" PRIx32 " %s "
      "rx_ring=%" PRId32 " rx_callback_calls=%" PRId32 " rx_callback_bytes=%" PRId32 " tx_bytes=%" PRId32 " tx_calls=%" PRId32
      " errors=%" PRId32 " state=%s tx_err=%u rx_err=%u %s",
      self->model_number,
      dtiox_zephyr_canbus_BITRATE,
      self->config.txid,
      self->config.use_extended_id ? "extended" : "standard",
      self->config.rx_ring_capacity,
      self->stats.rx_callback_calls,
      self->stats.rx_callback_bytes,
      self->stats.tx_bytes,
      self->stats.tx_calls,
      self->rx_error_count,
      dtiox_zephyr_canbus__state_to_str(self->cur_state),
      (unsigned)self->last_err_cnt.tx_err_cnt,
      (unsigned)self->last_err_cnt.rx_err_cnt,
      enabled_str);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Dispose
//
// - Stop the CAN controller.
// - Free FIFO storage.

void
dtiox_zephyr_canbus_dispose(dtiox_zephyr_canbus_t* self)
{
    if (!self)
        return;

    self->rx_enabled = false;

    if (self->can_dev && self->rx_filter_is_installed)
    {
        can_remove_rx_filter(self->can_dev, self->rx_filter_id);
        self->rx_filter_is_installed = false;
    }

    if (self->can_dev && self->dev_started)
    {
        (void)can_stop(self->can_dev);
        self->dev_started = false;
    }

    dtbuffer_dispose(self->rx_fifo_buffer);
}
