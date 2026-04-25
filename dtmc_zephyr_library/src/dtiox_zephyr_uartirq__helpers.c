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
dterr_t*
dtiox_zephyr_uartirq__tx_check(dtiox_zephyr_uartirq_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (!self->enabled)
    {
        dterr = dterr_new(DTERR_NOTREADY, DTERR_LOC, NULL, "TX not enabled");
        goto cleanup;
    }
    if (!self->dev)
    {
        dterr = dterr_new(DTERR_NOTREADY, DTERR_LOC, NULL, "device not attached");
        goto cleanup;
    }
    if (!self->tx_fifo_buffer)
    {
        dterr = dterr_new(DTERR_NOTREADY, DTERR_LOC, NULL, "TX FIFO not configured");
        goto cleanup;
    }
    if (self->stats.tx_error_count > 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "TX error count %" PRId32, self->stats.tx_error_count);
        goto cleanup;
    }
    if (self->stats.tx_dropped > 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "TX FIFO push dropped count %" PRId32, self->stats.tx_dropped);
        goto cleanup;
    }
cleanup:
    return dterr;
}

// ------------------------------------------------------------------------------
dterr_t*
dtiox_zephyr_uartirq__rx_check(dtiox_zephyr_uartirq_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (!self->enabled)
    {
        dterr = dterr_new(DTERR_NOTREADY, DTERR_LOC, NULL, "RX not enabled");
        goto cleanup;
    }
    if (!self->dev)
    {
        dterr = dterr_new(DTERR_NOTREADY, DTERR_LOC, NULL, "device not attached");
        goto cleanup;
    }
    if (!self->rx_fifo_buffer)
    {
        dterr = dterr_new(DTERR_NOTREADY, DTERR_LOC, NULL, "RX FIFO not configured");
        goto cleanup;
    }
    if (self->stats.rx_error_count > 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "RX error count %" PRId32, self->stats.rx_error_count);
        goto cleanup;
    }
    if (self->stats.rx_dropped > 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "RX FIFO push dropped count %" PRId32, self->stats.rx_dropped);
        goto cleanup;
    }
cleanup:
    return dterr;
}
