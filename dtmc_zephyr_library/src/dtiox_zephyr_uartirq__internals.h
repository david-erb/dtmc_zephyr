// this file contains miscellaneous internal helper functsions for the class
// doesn't contain callbacks or main methods

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

typedef struct dtiox_zephyr_uartirq_stats_t
{
    int32_t irq_count;
    int32_t irq_monitor_count;
    int32_t irq_error_count;
    int32_t tx_ready_count;
    int32_t tx_pushed;
    int32_t tx_dropped;
    int32_t tx_popped;
    int32_t tx_error_count;
    int32_t rx_ready_count;
    int32_t rx_error_count;
    int32_t rx_dropped;
    int32_t rx_popped;
} dtiox_zephyr_uartirq_stats_t;

typedef struct dtiox_zephyr_uartirq_t
{
    DTIOX_COMMON_MEMBERS
    bool _is_malloced;

    dtiox_zephyr_uartirq_config_t config;
    const struct device* dev;

    dtsemaphore_handle rx_semaphore;

    // ISR plumbing
    bool enabled;
    dtiox_zephyr_uartirq_stats_t stats;

    // TX FIFO: foreground consumer, ISR producer
    dtringfifo_t tx_fifo;
    dtbuffer_t* tx_fifo_buffer;

    // RX FIFO: ISR producer, foreground consumer
    dtringfifo_t rx_fifo;
    dtbuffer_t* rx_fifo_buffer;

} dtiox_zephyr_uartirq_t;

// helpers
extern dterr_t*
dtiox_zephyr_uartirq__tx_check(dtiox_zephyr_uartirq_t* self);

extern dterr_t*
dtiox_zephyr_uartirq__rx_check(dtiox_zephyr_uartirq_t* self);

// callbacks
extern void
dtiox_zephyr_uartirq__callback(const struct device* dev, void* user_data);
