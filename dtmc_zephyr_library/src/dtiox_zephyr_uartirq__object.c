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
#include <dtmc_base/dtuart_helpers.h>

#include <dtmc/dtiox_zephyr_uartirq.h>

#include "dtiox_zephyr_uartirq__internals.h"

// --------------------------------------------------------------------------------------------
// dtobject facade implementation
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Copy constructor
void
dtiox_zephyr_uartirq_copy(dtiox_zephyr_uartirq_t* this, dtiox_zephyr_uartirq_t* that)
{
    // this object does not support copying
    (void)this;
    (void)that;
}

// --------------------------------------------------------------------------------------------
// Equality check
bool
dtiox_zephyr_uartirq_equals(dtiox_zephyr_uartirq_t* a, dtiox_zephyr_uartirq_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    // TODO: Reconside equality semantics for dtiox_zephyr_uartirq_equals backend.
    return (a->model_number == b->model_number &&                                  //
            strcmp(a->config.device_tree_name, b->config.device_tree_name) == 0 && //
            a->config.uart_config.baudrate == b->config.uart_config.baudrate &&    //
            a->config.uart_config.parity == b->config.uart_config.parity &&        //
            a->config.uart_config.data_bits == b->config.uart_config.data_bits &&  //
            a->config.uart_config.stop_bits == b->config.uart_config.stop_bits &&  //
            a->config.uart_config.flow == b->config.uart_config.flow);
}

// --------------------------------------------------------------------------------------------
const char*
dtiox_zephyr_uartirq_get_class(dtiox_zephyr_uartirq_t* self)
{
    return "dtiox_zephyr_uartirq_t";
}

// --------------------------------------------------------------------------------------------

bool
dtiox_zephyr_uartirq_is_iface(dtiox_zephyr_uartirq_t* self, const char* iface_name)
{
    return strcmp(iface_name, DTIOX_IFACE_NAME) == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
// Convert to string
void
dtiox_zephyr_uartirq_to_string(dtiox_zephyr_uartirq_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    char tmp[64];
    dtuart_helper_to_string(&self->config.uart_config, tmp, sizeof(tmp));

    snprintf(buffer, buffer_size, "%s %s", self->config.device_tree_name, tmp);
    buffer[buffer_size - 1] = '\0';
}
