#include <errno.h>

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dtcore_helper.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>
#include <dtcore/dtringfifo.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtiox.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dtuart_helpers.h>

#include <dtmc/dtiox_zephyr_uartirq.h>

#include "dtiox_zephyr_uartirq__internals.h"

#define TAG "dtiox_zephyr_uartirq"
#define dtlog_debug(TAG, ...)

// vtable
DTIOX_INIT_VTABLE(dtiox_zephyr_uartirq);
DTOBJECT_INIT_VTABLE(dtiox_zephyr_uartirq);

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_create(dtiox_zephyr_uartirq_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    *self_ptr = (dtiox_zephyr_uartirq_t*)malloc(sizeof(**self_ptr));
    if (!*self_ptr)
        return dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc %zu", sizeof(**self_ptr));

    DTERR_C(dtiox_zephyr_uartirq_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr)
    {
        free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_zephyr_uartirq_create failed");
    }
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_init(dtiox_zephyr_uartirq_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_IOX_MODEL_ZEPHYR_UARTIRQ;

    DTERR_C(dtringfifo_init(&self->rx_fifo));

    DTERR_C(dtiox_set_vtable(self->model_number, &dtiox_zephyr_uartirq_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &dtiox_zephyr_uartirq_object_vt));

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtiox_zephyr_uartirq_init failed");
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_configure(dtiox_zephyr_uartirq_t* self, const dtiox_zephyr_uartirq_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);
    DTERR_ASSERT_NOT_NULL(config->device_tree_name);

    // validate UART config
    DTERR_C(dtuart_helper_validate(&config->uart_config));

    if (config->rx_capacity < 1)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "rx_capacity must be > 0, got %" PRId32, config->rx_capacity);
        goto cleanup;
    }
    if (config->tx_capacity < 1)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "tx_capacity must be > 0, got %" PRId32, config->tx_capacity);
        goto cleanup;
    }

    self->config = *config;

    // Allocate and configure RX FIFO storage
    {
        DTERR_C(dtbuffer_create(&self->rx_fifo_buffer, self->config.rx_capacity));
        dtringfifo_config_t c = { 0 };
        c.buffer = self->rx_fifo_buffer->payload;
        c.capacity = self->rx_fifo_buffer->length;
        DTERR_C(dtringfifo_configure(&self->rx_fifo, &c));
    }

    // Allocate and configure TX FIFO storage
    {
        DTERR_C(dtbuffer_create(&self->tx_fifo_buffer, self->config.tx_capacity));
        dtringfifo_config_t c = { 0 };
        c.buffer = self->tx_fifo_buffer->payload;
        c.capacity = self->tx_fifo_buffer->length;
        DTERR_C(dtringfifo_configure(&self->tx_fifo, &c));
    }

cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_attach(dtiox_zephyr_uartirq_t* self DTIOX_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    // TODO: In dtiox_zephyr_uartirq_attach() use a configuration option to control whether USB should be enabled.
    usb_enable(NULL);

    self->dev = device_get_binding(self->config.device_tree_name);
    if (!self->dev)
    {
        dterr =
          dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "device_tree_name \"%s\" cannot be found", self->config.device_tree_name);
        goto cleanup;
    }

    if (!device_is_ready(self->dev))
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "device_tree_name \"%s\" not ready", self->config.device_tree_name);
        goto cleanup;
    }

#ifndef CONFIG_UART_USE_RUNTIME_CONFIGURE
#error                                                                                                                         \
  "CONFIG_UART_USE_RUNTIME_CONFIGURE must be enabled in Zephyr config for dtiox_zephyr_uartirq to support runtime configuration"
#endif

    struct uart_config zcfg = { 0 };

    DTERR_NEGERROR_C(uart_config_get(self->dev, &zcfg));

    dtlog_debug(TAG,
      "initial zcfg.baudrate=%" PRIu32 " parity=%d data_bits=%d stop_bits=%d flow_ctrl=%d",
      zcfg.baudrate,
      zcfg.parity,
      zcfg.data_bits,
      zcfg.stop_bits,
      zcfg.flow_ctrl);

    zcfg.baudrate = (uint32_t)self->config.uart_config.baudrate;
    zcfg.parity = (self->config.uart_config.parity == DTUART_PARITY_NONE)  ? UART_CFG_PARITY_NONE
                  : (self->config.uart_config.parity == DTUART_PARITY_ODD) ? UART_CFG_PARITY_ODD
                                                                           : UART_CFG_PARITY_EVEN;
    zcfg.stop_bits = (self->config.uart_config.stop_bits == DTUART_STOPBITS_2) ? UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1;
    zcfg.data_bits = (self->config.uart_config.data_bits == DTUART_DATA_BITS_7) ? UART_CFG_DATA_BITS_7 : UART_CFG_DATA_BITS_8;
    zcfg.flow_ctrl =
      (self->config.uart_config.flow == DTUART_FLOW_RTSCTS) ? UART_CFG_FLOW_CTRL_RTS_CTS : UART_CFG_FLOW_CTRL_NONE;

    DTERR_NEGERROR_C(uart_configure(self->dev, &zcfg));

    DTERR_NEGERROR_C(uart_config_get(self->dev, &zcfg));

    dtlog_debug(TAG,
      "confirm zcfg.baudrate=%" PRIu32 " parity=%d data_bits=%d stop_bits=%d flow_ctrl=%d",
      zcfg.baudrate,
      zcfg.parity,
      zcfg.data_bits,
      zcfg.stop_bits,
      zcfg.flow_ctrl);

    dtlog_debug(TAG, "setting uart \"%s\" callback handler with context %p", self->config.device_tree_name, self);
    DTERR_NEGERROR_C(uart_irq_callback_user_data_set(self->dev, dtiox_zephyr_uartirq__callback, self));

cleanup:

    return dterr;
}

// ----------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_detach(dtiox_zephyr_uartirq_t* self DTIOX_DETACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_enable(dtiox_zephyr_uartirq_t* self DTIOX_ENABLE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->dev);

    self->enabled = false;

    uart_irq_tx_disable(self->dev);
    uart_irq_rx_disable(self->dev);

    // clear data already in the fifo after disabling

    dtringfifo_reset(&self->tx_fifo);
    dtringfifo_reset(&self->rx_fifo);

    memset(&self->stats, 0, sizeof(self->stats));

    if (enabled)
    {
        dtlog_debug(TAG, "enabling uart \"%s\" rx interrupts", self->config.device_tree_name);

        uart_irq_rx_enable(self->dev);

        self->enabled = true;
    }
    else
    {
        dtlog_debug(TAG, "disabled uart \"%s\" interrupts", self->config.device_tree_name);
    }

cleanup:

    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_read(dtiox_zephyr_uartirq_t* self DTIOX_READ_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->dev);

    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_read);

    DTERR_C(dtiox_zephyr_uartirq__rx_check(self));

    int32_t n = 0;

    // Non-blocking read from RX FIFO fed by IRQ handler.
    n = dtringfifo_pop(&self->rx_fifo, buf, buf_len);

    DTCORE_HELPER_ADD32(self->stats.rx_popped, n);

    *out_read = n;
cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_write(dtiox_zephyr_uartirq_t* self DTIOX_WRITE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(self->dev);

    DTERR_ASSERT_NOT_NULL(buf);
    DTERR_ASSERT_NOT_NULL(out_written);

    DTERR_C(dtiox_zephyr_uartirq__tx_check(self));

    int32_t n = 0;
    n = dtringfifo_push(&self->tx_fifo, buf, len);

    if (n > 0)
        uart_irq_tx_enable(self->dev);

    DTCORE_HELPER_ADD32(self->stats.tx_pushed, n);
    *out_written = n;

cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_set_rx_semaphore(dtiox_zephyr_uartirq_t* self DTIOX_SET_RX_SEMAPHORE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    self->rx_semaphore = rx_semaphore;

cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq_concat_format(dtiox_zephyr_uartirq_t* self DTIOX_CONCAT_FORMAT_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_str);

    char tmp[64];
    dtuart_helper_to_string(&self->config.uart_config, tmp, sizeof(tmp));

    *out_str = dtstr_concat_format(in_str,
      separator,
      "zephyr (%" PRId32 ") \"%s\" %s (rx_capacity=%" PRId32 ")"
      " irq %" PRId32 ", monitor %" PRId32 ", error %" PRId32 ", rx dropped %" PRId32 ", rx popped %" PRId32
      ", tx ready %" PRId32,
      self->model_number,
      self->config.device_tree_name,
      tmp,
      self->config.rx_capacity,
      self->stats.irq_count,
      self->stats.irq_monitor_count,
      self->stats.rx_error_count,
      self->stats.rx_dropped,
      self->stats.rx_popped,
      self->stats.tx_ready_count);

cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
void
dtiox_zephyr_uartirq_dispose(dtiox_zephyr_uartirq_t* self)
{
    if (!self)
        return;

    if (self->dev)
    {
        uart_irq_tx_disable(self->dev);
        uart_irq_rx_disable(self->dev);
    }

    dtbuffer_dispose(self->tx_fifo_buffer);
    dtbuffer_dispose(self->rx_fifo_buffer);

    if (self->_is_malloced)
        free(self);
    else
        memset(self, 0, sizeof(*self));
}
