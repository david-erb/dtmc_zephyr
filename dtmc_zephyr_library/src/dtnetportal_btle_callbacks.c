#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dtbytes.h>
#include <dtcore/dterr.h>
#include <dtcore/dtguid.h>
#include <dtcore/dtlog.h>

#include <dtmc/dtmc_zephyr.h>
#include <dtmc/dtnetportal_btle.h>

#include "dtnetportal_btle_private.h"

#define TAG "dtnetportal_btle"

#define dtlog_debug(TAG, ...)

// --------------------------------------------------------------------------------------

extern void
dtnetportal_btle_btenabled_callback(int err)
{
    dtnetportal_btle_t* self = dtnetportal_btle_global;

    if (err)
    {
        self->dterr_pile = dterr_new(DTERR_FAIL, DTERR_LOC, self->dterr_pile, "bluetooth initialization failed [err=%d]", err);
    }

    k_sem_give(&self->bt_init_ok);
}

// --------------------------------------------------------------------------------------
extern void
dtnetportal_btle_connected_callback(struct bt_conn* conn, uint8_t err)
{
    dtnetportal_btle_t* self = dtnetportal_btle_global;

    if (err)
    {
        dtlog_error(TAG, "connection failed (err %u)", err);
        self->conn = NULL;
    }
    else
    {
        dtlog_info(TAG, "connected to %p", (void*)conn);
        self->conn = conn;
    }
}

// --------------------------------------------------------------------------------------
extern void
dtnetportal_btle_mtu_updated_callback(struct bt_conn* conn, uint16_t tx, uint16_t rx)
{
    dtlog_debug(TAG, "ATT_MTU on %p updated: tx %u, rx %u", (void*)conn, tx, rx);
}

// --------------------------------------------------------------------------------------
extern void
dtnetportal_btle_disconnected_callback(struct bt_conn* conn, uint8_t reason)
{
    dtnetportal_btle_t* self = dtnetportal_btle_global;

    dtlog_info(TAG, "disconnected from %p (reason %u)", (void*)conn, reason);

    self->conn = NULL;

    // use disconnect as trigger to reuse the connection object (should be recycle event!)
    k_sem_give(&self->advertiser_restart_semaphore);
}

// --------------------------------------------------------------------------------------
// TODO: Handle logic change for BT_RPC where dtnetportal_btle_recycled_callback never gets called.
extern void
dtnetportal_btle_recycled_callback(void)
{
    dtnetportal_btle_t* self = dtnetportal_btle_global;

    dtlog_debug(TAG, "recycled connection object now available");

    k_sem_give(&self->advertiser_restart_semaphore); // signal the advertiser task to start advertising
}

// --------------------------------------------------------------------------------------
// CCCD change handler
extern void
dtnetportal_btle_ccc_cfg_changed_callback(const struct bt_gatt_attr* attr, uint16_t value)
{
    if (value == BT_GATT_CCC_NOTIFY)
    {
        dtlog_debug(TAG, "notifications enabled");
    }
    else
    {
        dtlog_debug(TAG, "notifications disabled");
    }
}

// --------------------------------------------------------------------------------------
// called when connected central device issues a GATT Read request
extern ssize_t
dtnetportal_btle_read_callback(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset)
{
    // dtnetportal_btle_t* self = dtnetportal_btle_global;

    // provide the value that will be sent back to the central during a GATT read operation.
    // return bt_gatt_attr_read(conn, attr, buf, len, offset, self->value, self->value_len);

    dtlog_debug(TAG, "dtnetportal_btle_read_callback called for attr %p, returning len %d", (void*)attr, (int)len);
    return len;
}

// --------------------------------------------------------------------------------------
// called when a connected central device writes to the characteristic
extern ssize_t
dtnetportal_btle_write_callback(struct bt_conn* conn,
  const struct bt_gatt_attr* attr,
  const void* buf,
  uint16_t len,
  uint16_t offset,
  uint8_t flags)
{
    dtnetportal_btle_t* self = dtnetportal_btle_global;
    dterr_t* dterr = NULL;
    dtbuffer_t* buffer = NULL;

    // wrap a buffer around the data that was sent by the central device (not copying it here)
    DTERR_C(dtbuffer_create(&buffer, 0));
    // substitute for the payload
    buffer->payload = (void*)buf;
    buffer->length = len;

    // user data is the subject name for the manifold
    const char* subject_name = attr->user_data;

#ifndef dtlog_debug
    {
        char tmp[64];
        dtbytes_compose_hex(buf, len, tmp, sizeof(tmp));
        dtlog_debug(TAG, "data received on topic \"%s\" len %d %s", subject_name, len, tmp);
    }
#endif

    // publish the data to the manifold (it will make a copy of the buffer for each recipient)
    DTERR_C(dtmanifold_publish(self->manifold, subject_name, buffer));

cleanup:
    dtbuffer_dispose(buffer);

    // TODO: Consider how to deal with an error in dtnetportal_btle_write_callback.
    if (dterr != NULL)
    {
        dtlog_error(TAG, "%s failed: %s", __func__, dterr->message);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    else
        return len;
}

// --------------------------------------------------------------------------------------

extern void
dtnetportal_btle_cfgchanged_callback(const struct bt_gatt_attr* attr, uint16_t value)
{
    // this is called when the Central device sets or un-sets its desire to get notifications if the data changes
    // notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}
