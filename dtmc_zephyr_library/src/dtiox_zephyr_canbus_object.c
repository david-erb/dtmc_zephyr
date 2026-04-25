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

// --------------------------------------------------------------------------------------------
// dtobject implementation
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Copy constructor
void
dtiox_zephyr_canbus_copy(dtiox_zephyr_canbus_t* this, dtiox_zephyr_canbus_t* that)
{
    // this object does not support copying
    (void)this;
    (void)that;
}

// --------------------------------------------------------------------------------------------
// Equality check
bool
dtiox_zephyr_canbus_equals(dtiox_zephyr_canbus_t* a, dtiox_zephyr_canbus_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    // TODO: Reconside equality semantics for dtiox_zephyr_canbus_equals backend.
    return (a->model_number == b->model_number && //
            a->config.txid == b->config.txid);
}

// --------------------------------------------------------------------------------------------
const char*
dtiox_zephyr_canbus_get_class(dtiox_zephyr_canbus_t* self)
{
    return "dtiox_zephyr_canbus_t";
}

// --------------------------------------------------------------------------------------------

bool
dtiox_zephyr_canbus_is_iface(dtiox_zephyr_canbus_t* self, const char* iface_name)
{
    return strcmp(iface_name, DTIOX_IFACE_NAME) == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
// Convert to string
void
dtiox_zephyr_canbus_to_string(dtiox_zephyr_canbus_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    snprintf(buffer, buffer_size, "txid=0x%04" PRIx32 "@%" PRId32, self->config.txid, (int32_t)dtiox_zephyr_canbus_BITRATE);
    buffer[buffer_size - 1] = '\0';
}
