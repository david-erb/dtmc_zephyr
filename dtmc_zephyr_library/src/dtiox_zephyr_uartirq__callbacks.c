// this file contains internal callback methods for the class

#include <errno.h>

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

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

#include <dtmc/dtiox_zephyr_uartirq.h>

#include "dtiox_zephyr_uartirq__internals.h"

#define TAG "dtiox_zephyr_uartirq"

// ------------------------------------------------------------------------------
void
dtiox_zephyr_uartirq__tx(dtiox_zephyr_uartirq_t* self, const struct device* dev)
{
    // we'll transfer up to 32 bytes at a time
    uint8_t buf[32];

    // get from what the user has put into the TX ringfifo
    int32_t n_to_write = dtringfifo_pop(&self->tx_fifo, buf, sizeof(buf));

    if (n_to_write == 0)
    {
        uart_irq_tx_disable(dev);
        return;
    }

    // write to UART FIFO
    int32_t n_written = uart_fifo_fill(dev, buf, n_to_write);

    // some error?
    if (n_written < 0)
    {
        DTCORE_HELPER_INC32(self->stats.tx_error_count);
        uart_irq_tx_disable(dev);
    }
    else if (n_written < n_to_write)
    {
        DTCORE_HELPER_ADD32(self->stats.tx_dropped, n_to_write - n_written);
        uart_irq_tx_disable(dev);
    }
}

// ------------------------------------------------------------------------------
void
dtiox_zephyr_uartirq__rx(dtiox_zephyr_uartirq_t* self, const struct device* dev)
{
    bool any_new_bytes = false;

    for (;;)
    {
        uint8_t buf[64];
        int n = uart_fifo_read(dev, buf, sizeof(buf));
        if (n == 0)
            break;

        if (n < 0)
        {
            DTCORE_HELPER_INC32(self->stats.rx_error_count);
            break;
        }

        any_new_bytes = true;

        // Store as many bytes as will fit into the FIFO.
        int32_t written = dtringfifo_push(&self->rx_fifo, buf, n);
        if (written < n)
        {
            DTCORE_HELPER_ADD32(self->stats.rx_dropped, n - written);
            break;
        }
    }

    if (any_new_bytes && self->rx_semaphore)
    {
        dtsemaphore_post(self->rx_semaphore);
    }
}

// ------------------------------------------------------------------------------
// irq handler
void
dtiox_zephyr_uartirq__callback(const struct device* dev, void* user_data)
{
    dtiox_zephyr_uartirq_t* self = (dtiox_zephyr_uartirq_t*)user_data;
    if (self == NULL)
        return;

    if (self->dev == NULL)
        return;

    DTCORE_HELPER_INC32(self->stats.irq_count);

    if (!uart_irq_update(dev))
    {
        DTCORE_HELPER_INC32(self->stats.irq_error_count);
        return;
    }

    if (uart_irq_tx_ready(dev))
    {
        DTCORE_HELPER_INC32(self->stats.tx_ready_count);
        dtiox_zephyr_uartirq__tx(self, dev);
    }
    if (uart_irq_rx_ready(dev))
    {
        DTCORE_HELPER_INC32(self->stats.rx_ready_count);
        dtiox_zephyr_uartirq__rx(self, dev);
    }
}