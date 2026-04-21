#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtiox_modbus.h>
#include <dtmc_base/dtlock.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dtuart_helpers.h>

#include <dtmc/dtiox_zephyr_modbus_slave.h>

#include "dtiox_zephyr_modbus_slave__internals.h"

#define TAG "dtiox_zephyr_modbus_slave"

DTIOX_INIT_VTABLE(dtiox_zephyr_modbus_slave);
DTOBJECT_INIT_VTABLE(dtiox_zephyr_modbus_slave);

// -----------------------------------------------------------------------------
// Public lifecycle

dterr_t*
dtiox_zephyr_modbus_slave_create(dtiox_zephyr_modbus_slave_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    dtiox_zephyr_modbus_slave_t* self = (dtiox_zephyr_modbus_slave_t*)calloc(1, sizeof(*self));
    if (self == NULL)
        return dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "calloc failed");

    DTERR_C(dtiox_zephyr_modbus_slave_init(self));

    self->_is_malloced = true;
    *self_ptr = self;
cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave_init(dtiox_zephyr_modbus_slave_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_IOX_MODEL_ZEPHYR_MODBUS_SLAVE;

    // RX fifo will be configured for size in configure().
    DTERR_C(dtringfifo_init(&self->rx_fifo));

    // Register vtables
    DTERR_C(dtiox_set_vtable(self->model_number, &dtiox_zephyr_modbus_slave_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &dtiox_zephyr_modbus_slave_object_vt));

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave_configure(dtiox_zephyr_modbus_slave_t* self, const dtiox_zephyr_modbus_slave_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);

    // validate UART config
    DTERR_C(dtuart_helper_validate(&config->uart_config));

    if (config->rx_ring_capacity < 1)
    {
        dterr =
          dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "rx_ring_capacity must be > 0, got %" PRId32, config->rx_ring_capacity);
        goto cleanup;
    }

    self->config = *config;

    // Allocate and configure RX FIFO storage
    {
        DTERR_C(dtbuffer_create(&self->rx_fifo_buffer, self->config.rx_ring_capacity));

        dtringfifo_config_t c = { 0 };
        c.buffer = self->rx_fifo_buffer->payload;
        c.capacity = self->rx_fifo_buffer->length;

        DTERR_C(dtringfifo_configure(&self->rx_fifo, &c));
    }

    // Allocate and configure TX staged storage
    {
        DTERR_C(dtbuffer_create(&self->tx_staged_buf, DTIOX_MODBUS_MAX_BLOB_BYTES));
    }

    self->rx_overflow_pending = false;

    self->tx_staged_len = 0;
    self->tx_staged_pending = false;

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// dtiox facade implementation

dterr_t*
dtiox_zephyr_modbus_slave_attach(dtiox_zephyr_modbus_slave_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    // if already listening, don't listen again
    if (self->_is_modbus_init_server)
    {
        goto cleanup;
    }

    // create lock betwenween API and modbus callbacks
    DTERR_C(dtlock_create(&self->lock_handle));

    DTERR_C(dtlock_acquire(self->lock_handle));

    // Bind singleton instance for modbus callbacks.
    dtiox_zephyr_modbus_slave__callbacks_set_self(self);

    int iface = modbus_iface_get_by_name(self->config.device_tree_name);
    if (iface < 0)
    {
        dterr = dterr_new(DTERR_NOTFOUND,
          DTERR_LOC,
          NULL,
          "modbus_iface_get_by_name(%s) failed: " DTERR_NEGERROR_FORMAT,
          self->config.device_tree_name,
          DTERR_NEGERROR_ARGS(iface));
        goto release_lock_and_cleanup;
    }

    self->iface = iface;

    struct modbus_iface_param param;
    memset(&param, 0, sizeof(param));

    param.mode = MODBUS_MODE_RTU;

    param.server.user_cb = &dtiox_zephyr_modbus_slave__user_cbs;
    param.server.unit_id = (uint8_t)self->config.unit_id;

    // Serial parameters (some backends may use devicetree config; setting these is harmless).
    param.serial.baud = (uint32_t)self->config.uart_config.baudrate;

    switch (self->config.uart_config.parity)
    {
        case DTUART_PARITY_EVEN:
            param.serial.parity = UART_CFG_PARITY_EVEN;
            break;
        case DTUART_PARITY_ODD:
            param.serial.parity = UART_CFG_PARITY_ODD;
            break;
        case DTUART_PARITY_NONE:
        default:
            param.serial.parity = UART_CFG_PARITY_NONE;
            break;
    }

    switch (self->config.uart_config.stop_bits)
    {
        case DTUART_STOPBITS_2:
            param.serial.stop_bits_client = UART_CFG_STOP_BITS_2;
            break;
        case DTUART_STOPBITS_1:
        default:
            param.serial.stop_bits_client = UART_CFG_STOP_BITS_1;
            break;
    }

    {
        char s[256];
        dtobject_to_string((dtobject_handle)self, s, sizeof(s));
        dtlog_info(TAG, "iox object is: %s", s);
    }

    int rc = modbus_init_server(self->iface, param);
    if (rc != 0)
    {
        dterr =
          dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "modbus_init_server failed: " DTERR_NEGERROR_FORMAT, DTERR_NEGERROR_ARGS(rc));
        goto release_lock_and_cleanup;
    }

    // mark as listening, will never stop listening in this implementation
    self->_is_modbus_init_server = true;

release_lock_and_cleanup:
    dterr = dtiox_zephyr_modbus_slave__release_lock(self, dterr);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave_detach(dtiox_zephyr_modbus_slave_t* self)
{
    dterr_t* dterr = NULL;

    // detach is a no-op in this implementation
    // we expect to be alive for the entire firmware lifetime and always listening once attached

    return dterr;
}

// -----------------------------------------------------------------------------
// this implementation doesn't stop interrupts or incoming data
// it just stops processing data in the callbacks

dterr_t*
dtiox_zephyr_modbus_slave_enable(dtiox_zephyr_modbus_slave_t* self, bool enabled)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->lock_handle);

    DTERR_C(dtlock_acquire(self->lock_handle));

    self->_is_callback_processing_enabled = enabled;

    // when enabling, clear state for predictable behavior
    if (enabled)
    {
        dtringfifo_reset(&self->rx_fifo);
        self->rx_overflow_pending = false;

        self->tx_staged_pending = false;
        self->tx_staged_len = 0;

        self->s2m_status = (uint16_t)DTIOX_MODBUS_STATUS_NO_DATA;
        self->s2m_len = 0;
        memset(self->s2m_data_regs, 0, sizeof(self->s2m_data_regs));

        memset(&self->stats, 0, sizeof(self->stats));
    }

    dterr = dtiox_zephyr_modbus_slave__release_lock(self, dterr);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave_read(dtiox_zephyr_modbus_slave_t* self, uint8_t* buf, int32_t buf_len, int32_t* out_read)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_read);
    DTERR_ASSERT_NOT_NULL(self->lock_handle);

    if (buf_len < 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "buf_len < 0");
        goto cleanup;
    }

    DTERR_C(dtlock_acquire(self->lock_handle));

    if (self->rx_overflow_pending)
    {
        self->rx_overflow_pending = false;
        dterr = dterr_new(DTERR_OVERFLOW, DTERR_LOC, NULL, "rx fifo overflow");
        goto release_lock_and_cleanup;
    }

    int32_t n = dtringfifo_pop(&self->rx_fifo, buf, buf_len);
    *out_read = n;

release_lock_and_cleanup:
    dterr = dtiox_zephyr_modbus_slave__release_lock(self, dterr);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave_write(dtiox_zephyr_modbus_slave_t* self, const uint8_t* buf, int32_t len, int32_t* out_written)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_written);
    DTERR_ASSERT_NOT_NULL(self->lock_handle);
    DTERR_ASSERT_NOT_NULL(self->tx_staged_buf);
    DTERR_ASSERT_NOT_NULL(self->tx_staged_buf->payload);

    if (len < 0)
        return dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "len < 0");

    DTERR_C(dtlock_acquire(self->lock_handle));

    // Only allow one outstanding message to the master at a time.
    if (self->tx_staged_pending)
    {
        self->stats.tx_dropped += len;
        *out_written = 0;
        dterr = dterr_new(DTERR_BUSY, DTERR_LOC, NULL, "tx already pending");
        goto release_lock_and_cleanup;
    }

    int32_t send_len = (len > DTIOX_MODBUS_MAX_BLOB_BYTES) ? DTIOX_MODBUS_MAX_BLOB_BYTES : len;

    memcpy(self->tx_staged_buf->payload, buf, (size_t)send_len);
    self->tx_staged_len = send_len;
    self->tx_staged_pending = (send_len > 0);

    *out_written = send_len;

release_lock_and_cleanup:
    dterr = dtiox_zephyr_modbus_slave__release_lock(self, dterr);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave_set_rx_semaphore(dtiox_zephyr_modbus_slave_t* self, dtsemaphore_handle rx_semaphore)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    // we might be called before attach() so there might not be a lock yet
    bool lockable = self->lock_handle != NULL;

    if (lockable)
    {
        DTERR_C(dtlock_acquire(self->lock_handle));
    }

    self->rx_semaphore = rx_semaphore;

    if (lockable)
    {
        dterr = dtiox_zephyr_modbus_slave__release_lock(self, dterr);
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_modbus_slave_concat_format(dtiox_zephyr_modbus_slave_t* self, char* in_str, char* separator, char** out_str)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_str);

    dtuart_helper_config_t uart_cfg;
    dtiox_zephyr_modbus_slave_stats_t stats;

    // grab a copy under lock
    if (self->lock_handle != NULL)
        DTERR_C(dtlock_acquire(self->lock_handle));
    uart_cfg = self->config.uart_config;
    stats = self->stats;
    if (self->lock_handle != NULL)
        DTERR_C(dtlock_release(self->lock_handle));

    char tmp[256];
    dtuart_helper_to_string(&uart_cfg, tmp, sizeof(tmp));

    *out_str = dtstr_concat_format(in_str,
      separator,
      "zephyr_modbus %s:%" PRId32 " %s"
      " m2s_polls=%" PRId32 " m2s_blobs=%" PRId32 " s2m_transfers=%" PRId32,
      self->config.device_tree_name,
      self->config.unit_id,
      tmp,
      stats.m2s_polls_started_count,
      stats.m2s_blobs_started_count,
      stats.s2m_transfers_started_count);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// detach is a no-op in this implementation
// we expect to be alive for the entire firmware lifetime and always listening once attached

void
dtiox_zephyr_modbus_slave_dispose(dtiox_zephyr_modbus_slave_t* self)
{
}
