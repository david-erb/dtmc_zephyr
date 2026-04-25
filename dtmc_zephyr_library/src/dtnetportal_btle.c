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
#include <dtcore/dterr.h>
#include <dtcore/dtguid.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtmanifold.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc/dtmc_zephyr.h>
#include <dtmc/dtnetportal_btle.h>

#include <dtmc_base/dtnetportal.h>

#include "dtnetportal_btle_private.h"

DTNETPORTAL_INIT_VTABLE(dtnetportal_btle);
DTOBJECT_INIT_VTABLE(dtnetportal_btle);

#define TAG "dtnetportal_btle"

// comment out the logging here
#define dtlog_debug(TAG, ...)

dtnetportal_btle_t* dtnetportal_btle_global = NULL;

// --------------------------------------------------------------------------------------

#define SERVICE_UUID_VAL BT_UUID_128_ENCODE DTMC_BASE_CONSTANTS_DTNETPORTAL_BTLE_SERVICE_NAME_GATT_SVC_UUID
#define BROADCAST_COMMAND_TOPIC_UUID_VAL BT_UUID_128_ENCODE DTMC_BASE_CONSTANTS_BROADCAST_COMMAND_TOPIC_GATT_CHRC_UUID
#define EMITTER_INJECTION_TOPIC_UUID_VAL BT_UUID_128_ENCODE DTMC_BASE_CONSTANTS_EMITTER_INJECTION_TOPIC_GATT_CHRC_UUID
#define REFLECTOR_CENSUS_TOPIC_UUID_VAL BT_UUID_128_ENCODE DTMC_BASE_CONSTANTS_REFLECTOR_CENSUS_TOPIC_GATT_CHRC_UUID

#define SERVICE_UUID BT_UUID_DECLARE_128(SERVICE_UUID_VAL)
#define BROADCAST_COMMAND_TOPIC_UUID BT_UUID_DECLARE_128(BROADCAST_COMMAND_TOPIC_UUID_VAL)
#define EMITTER_INJECTION_TOPIC_UUID BT_UUID_DECLARE_128(EMITTER_INJECTION_TOPIC_UUID_VAL)
#define REFLECTOR_CENSUS_TOPIC_UUID BT_UUID_DECLARE_128(REFLECTOR_CENSUS_TOPIC_UUID_VAL)

BT_GATT_SERVICE_DEFINE(dtnetportal_btle_gatt_service,
  BT_GATT_PRIMARY_SERVICE(SERVICE_UUID),
  // Broadcast Command Topic
  BT_GATT_CHARACTERISTIC(BROADCAST_COMMAND_TOPIC_UUID,
    BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
    dtnetportal_btle_read_callback,
    dtnetportal_btle_write_callback,
    DTMC_BASE_CONSTANTS_BROADCAST_COMMAND_TOPIC),
  BT_GATT_CUD(DTMC_BASE_CONSTANTS_BROADCAST_COMMAND_TOPIC, BT_GATT_PERM_READ),
  BT_GATT_CCC(dtnetportal_btle_ccc_cfg_changed_callback, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
  // Emitter Injection Topic
  BT_GATT_CHARACTERISTIC(EMITTER_INJECTION_TOPIC_UUID,
    BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
    dtnetportal_btle_read_callback,
    dtnetportal_btle_write_callback,
    DTMC_BASE_CONSTANTS_EMITTER_INJECTION_TOPIC),
  BT_GATT_CUD(DTMC_BASE_CONSTANTS_EMITTER_INJECTION_TOPIC, BT_GATT_PERM_READ),
  BT_GATT_CCC(dtnetportal_btle_ccc_cfg_changed_callback, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
  // Reflector Census Topic
  BT_GATT_CHARACTERISTIC(REFLECTOR_CENSUS_TOPIC_UUID,
    BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
    dtnetportal_btle_read_callback,
    dtnetportal_btle_write_callback,
    DTMC_BASE_CONSTANTS_REFLECTOR_CENSUS_TOPIC),
  BT_GATT_CUD(DTMC_BASE_CONSTANTS_REFLECTOR_CENSUS_TOPIC, BT_GATT_PERM_READ),
  BT_GATT_CCC(dtnetportal_btle_ccc_cfg_changed_callback, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

// --------------------------------------------------------------------------------------
extern dterr_t*
dtnetportal_btle_create(dtnetportal_btle_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (dtnetportal_btle_t*)k_malloc(sizeof(dtnetportal_btle_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(
          DTERR_NOMEM, DTERR_LOC, NULL, "failed to allocate %zu bytes for dtnetportal_btle_t", sizeof(dtnetportal_btle_t));
        goto cleanup;
    }

    DTERR_C(dtnetportal_btle_init(*self_ptr));

    (*self_ptr)->_is_malloced = true;

cleanup:

    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
        {
            k_free(*self_ptr);
        }

        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtnetportal_btle_create failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
// map the topic to the attribute which holds the characteristic value
// we need this attribute when we want to notify the central of a change to the characteristic value
static dterr_t*
dtnetportal_btle_set_topic_to_attr_map(dtnetportal_btle_t* self, const char* topic)
{
    dterr_t* dterr = NULL;

    if (self->topic_to_attr_map_count >= MAX_CHARACTERISTICS)
    {
        dterr = dterr_new(DTERR_OVERFLOW, DTERR_LOC, NULL, "maximum number of characteristics %d reached", MAX_CHARACTERISTICS);
        goto cleanup;
    }

    // get the attr for the characteristic value
    const struct bt_gatt_attr* attr = NULL;

    for (int i = 0; i < dtnetportal_btle_gatt_service.attr_count; i++)
    {
        attr = &dtnetportal_btle_gatt_service.attrs[i];

        if (attr == NULL)
            continue;

        if (attr->uuid == NULL)
            continue;

        if (bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CHRC) != 0)
            continue;

        attr = &dtnetportal_btle_gatt_service.attrs[i + 1]; // the next attr is the characteristic value

        if (attr->user_data == NULL)
            continue;

        if (strcmp((const char*)attr->user_data, topic) != 0)
            continue;

        break;
    }

    if (attr == NULL)
    {
        dterr = dterr_new(DTERR_NOTFOUND, DTERR_LOC, NULL, "characteristic not found for topic \"%s\"", topic);
        goto cleanup;
    }

    // Store the topic and its attribute
    topic_to_attr_map_t* topic_to_attr_entry = &self->topic_to_attr_map[self->topic_to_attr_map_count];

    topic_to_attr_entry->topic = topic;
    topic_to_attr_entry->attr = attr;

    {
        char uuid_str[37];
        bt_uuid_to_str(attr->uuid, uuid_str, sizeof(uuid_str));
        dtlog_debug(TAG, "mapping topic \"%s\" to attr %p (UUID: %s)", topic, (void*)attr, uuid_str);
    }

    self->topic_to_attr_map_count++;

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "%s failed for topic \"%s\"", __func__, topic);
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
static dterr_t*
dtnetportal_btle_get_topic_to_attr_map(dtnetportal_btle_t* self, const char* topic, const struct bt_gatt_attr** attr_ptr)
{
    dterr_t* dterr = NULL;

    if (self == NULL || topic == NULL || attr_ptr == NULL)
    {
        dterr = dterr_new(DTERR_ARGUMENT_NULL, DTERR_LOC, NULL, "invalid arguments");
        goto cleanup;
    }

    for (int i = 0; i < self->topic_to_attr_map_count; i++)
    {
        if (strcmp(self->topic_to_attr_map[i].topic, topic) == 0)
        {
            *attr_ptr = self->topic_to_attr_map[i].attr;
            goto cleanup;
        }
    }

    dterr = dterr_new(DTERR_NOTFOUND, DTERR_LOC, NULL, "handle not found for topic \"%s\"", topic);

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "%s failed", __func__);
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_btle_init(dtnetportal_btle_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_NETPORTAL_MODEL_BTLE;

    // set the vtable for this model number
    DTERR_C(dtnetportal_set_vtable(self->model_number, &dtnetportal_btle_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &_object_vt));

    k_sem_init(&self->bt_init_ok, 1, 1);

    {

        dttasker_config_t c = { 0 };
        c.name = "dtnetportal_btle";
        c.stack_size = 4096;
        c.priority = K_PRIO_PREEMPT(8);
        c.tasker_entry_point_fn = dtnetportal_btle_advertiser_task_entrypoint;
        c.tasker_entry_point_arg = self;

        DTERR_C(dttasker_create(&self->advertising_tasker, &c));
    }

    // set semaphore we share with the advertiser task as "taken"
    k_sem_init(&self->advertiser_restart_semaphore, 0, 1);

    self->manifold = &self->_manifold;
    DTERR_C(dtmanifold_init(self->manifold));

    DTERR_C(dtnetportal_btle_set_topic_to_attr_map(self, DTMC_BASE_CONSTANTS_BROADCAST_COMMAND_TOPIC));
    DTERR_C(dtnetportal_btle_set_topic_to_attr_map(self, DTMC_BASE_CONSTANTS_EMITTER_INJECTION_TOPIC));
    DTERR_C(dtnetportal_btle_set_topic_to_attr_map(self, DTMC_BASE_CONSTANTS_REFLECTOR_CENSUS_TOPIC));

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtnetportal_btle_init failed");
    return dterr;
}

// --------------------------------------------------------------------------------------
// here we start building the GATT service attributes starting with the service itself
dterr_t*
dtnetportal_btle_configure(dtnetportal_btle_t* self, dtnetportal_btle_config_t* config)
{
    dterr_t* dterr = NULL;
    self->config = *config;
    return dterr;
}

// --------------------------------------------------------------------------------------

static void
dthelper_printf_static_gatt_svc(const struct bt_gatt_service_static* svc)
{
    char uuid_str[37];

    /* Header */
    printf("GATT PERM values are: read 0x%04x, write 0x%04x\n", BT_GATT_PERM_READ, BT_GATT_PERM_WRITE);
    printf("GATT CHRC properties are: read 0x%02x, write 0x%02x, notify 0x%02x\n",
      BT_GATT_CHRC_READ,
      BT_GATT_CHRC_WRITE,
      BT_GATT_CHRC_NOTIFY);
    printf("GATT primary service UUID: 0x%08x\n", BT_UUID_GATT_PRIMARY_VAL);
    printf("GATT include service UUID: 0x%08x\n", BT_UUID_GATT_INCLUDE_VAL);
    printf("GATT characteristic UUID: 0x%08x\n", BT_UUID_GATT_CHRC_VAL);
    printf("GATT CCCD UUID: 0x%08x\n", BT_UUID_GATT_CCC_VAL);
    printf("GATT Service @ %p: %zu attrs\n", svc, svc->attr_count);
    printf("| idx | handle | UUID                                 | perm   |       read |      write | user_data  |\n");
    printf("|-----|--------|--------------------------------------|--------|------------|------------|------------|\n");

    for (size_t i = 0; i < svc->attr_count; i++)
    {
        const struct bt_gatt_attr* attr = &svc->attrs[i];

        /* Convert UUID to string */
        bt_uuid_to_str(attr->uuid, uuid_str, sizeof(uuid_str));

        printf("| %3zu | 0x%04x | %-36s | 0x%04x | %10p | %10p | %10p |\n",
          i,
          attr->handle,
          uuid_str,
          attr->perm,
          (void*)attr->read,
          (void*)attr->write,
          attr->user_data);
    }
}

// --------------------------------------------------------------------------------------
// this function will register the GATT service and start advertising

dterr_t*
dtnetportal_btle_activate(dtnetportal_btle_t* self)
{
    dterr_t* dterr = NULL;
    int err;

    if (dtnetportal_btle_global != NULL)
    {
        dterr = dterr_new(DTERR_EXISTS, DTERR_LOC, NULL, "dtnetportal_btle_activate already called");
        goto cleanup;
    }

    dtnetportal_btle_global = self;
    err = bt_enable(dtnetportal_btle_btenabled_callback);
    if (err)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "bt_enable failed with error %d", err);
        goto cleanup;
    }
    dtlog_debug(TAG, "bt_enable called, waiting for callback");

    // Wait for Bluetooth to be initialized
    err = k_sem_take(&self->bt_init_ok, K_FOREVER);
    dtlog_debug(TAG, "bt_enable callback complete");

    if (err)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "k_sem_take failed with error %d", err);
        goto cleanup;
    }

    // check if there was an error during dtnetportal_btle_btenabled_callback
    if (self->dterr_pile != NULL)
    {
        dterr = self->dterr_pile;
        goto cleanup;
    }

    self->connection_callbacks.connected = dtnetportal_btle_connected_callback;
    self->connection_callbacks.disconnected = dtnetportal_btle_disconnected_callback;
    self->connection_callbacks.recycled = dtnetportal_btle_recycled_callback;

    // Register the connection callbacks dynamically
    err = bt_conn_cb_register(&self->connection_callbacks);
    if (err)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "bt_conn_cb_register failed with error %d", err);
        goto cleanup;
    }

    // Register the GATT callbacks dynamically
    self->gatt_callbacks.att_mtu_updated = dtnetportal_btle_mtu_updated_callback;
    bt_gatt_cb_register(&self->gatt_callbacks);

    dthelper_printf_static_gatt_svc(&dtnetportal_btle_gatt_service);
    dtlog_debug(TAG, "bt_gatt_service_register NOT called, using static service");

    dtlog_debug(TAG, "setting up advertising data as device name \"%s\"", self->config.device_name);

    // Advertising Data (AD)
    self->ad[0] = (struct bt_data)BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));

    const struct bt_uuid_128* service_uuid = (const struct bt_uuid_128*)dtnetportal_btle_gatt_service.attrs[0].user_data;

    // Advertising Data: include UUID
    self->ad[1] = (struct bt_data){
        .type = BT_DATA_UUID128_ALL,
        .data_len = 16,
        .data = service_uuid->val,
    };

    // Scan Response Data: include name
    self->sd[0] = (struct bt_data){
        .type = BT_DATA_NAME_COMPLETE,
        .data_len = strlen(self->config.device_name),
        .data = (const uint8_t*)self->config.device_name,
    };

    dtlog_debug(TAG, "starting advertising tasker");

    DTERR_C(dtnetportal_btle_start_advertising_tasker(self));

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "%s failed", __func__);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_btle_start_advertising_tasker(dtnetportal_btle_t* self)
{
    dterr_t* dterr = NULL;

    dtlog_info(TAG, "starting task for advertising on self %p semaphore %p", self, &self->advertiser_restart_semaphore);

    DTERR_C(dttasker_start(self->advertiser_tasker));

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "%s failed", __func__);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_btle_start_advertising(dtnetportal_btle_t* self)
{
    dterr_t* dterr = NULL;

    int max_tries = 10;
    int nap_time_ms = 100;
    int err = 0;
    int i;
    for (i = 0; i < max_tries; i++)
    {
        dtlog_debug(TAG, "starting advertising try %d/%d", i + 1, max_tries);
        err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, self->ad, ARRAY_SIZE(self->ad), self->sd, ARRAY_SIZE(self->sd));
        if (!err)
            break;
        if (i < max_tries - 1)
            k_sleep(K_MSEC(nap_time_ms)); // wait before retrying
    }

    if (err)
    {
        dterr = dterr_new(
          DTERR_FAIL, DTERR_LOC, self->dterr_pile, "advertising failed to start after %d tries [err=%d]", max_tries, err);
        goto cleanup;
    }
    else
    {
        dtlog_debug(TAG, "advertising successfully started after %d tries", i + 1);
    }

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_btle_subscribe(dtnetportal_btle_t* self,
  const char* topic,
  void* recipient_self,
  dtnetportal_receive_callback_t receiver_callback)
{
    dterr_t* dterr = NULL;

    if (self->manifold == NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "%s called before manifold initialized", __func__);
        goto cleanup;
    }

    if (topic == NULL || receiver_callback == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "%s called with NULL topic or callback", __func__);
        goto cleanup;
    }

    dtlog_debug(TAG,
      "subscribing to topic \"%s\" at callback %p for recipient self %p",
      topic,
      (void*)receiver_callback,
      (void*)recipient_self);

    // TODO: Consider checking dtnetportal_receive_callback_t should be presume same as dtmanifold_receiver_callback_t.
    DTERR_C(dtmanifold_subscribe(self->manifold, topic, recipient_self, receiver_callback));

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_btle_publish(dtnetportal_btle_t* self, const char* topic, dtbuffer_t* buffer)
{
    dterr_t* dterr = NULL;

    if (topic == NULL || buffer == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "%s called with NULL topic or buffer", __func__);
        goto cleanup;
    }

    if (self->conn == NULL)
    {
        dterr = dterr_new(DTERR_UNREACHABLE, DTERR_LOC, NULL, "not connected to the BTLE central");
        goto cleanup;
    }

    dtlog_debug(TAG, "sending %d bytes to topic \"%s\"", buffer->length, topic);

    const struct bt_gatt_attr* attr = NULL;
    DTERR_C(dtnetportal_btle_get_topic_to_attr_map(self, topic, &attr));

    {
        char uuid_str[37];
        bt_uuid_to_str(attr->uuid, uuid_str, sizeof(uuid_str));
        dtlog_debug(TAG, "sending topic \"%s\" to attr %p (UUID: %s)", topic, (void*)attr, uuid_str);
    }

    int rc = bt_gatt_notify(self->conn, attr, buffer->payload, buffer->length);

    if (rc < 0)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "bt_gatt_notify failed with error %d: %s", rc, strerror(-rc));
        goto cleanup;
    }

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "%s failed for topic \"%s\"", __func__, topic);
    }

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_btle_get_info(dtnetportal_btle_t* self, dtnetportal_info_t* info)
{
    dterr_t* dterr = NULL;
    if (info == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "called with NULL info");
        goto cleanup;
    }

    memset(info, 0, sizeof(*info));

    info->flavor = DTNETPORTAL_BTLE_FLAVOR;
    info->version = DTNETPORTAL_BTLE_VERSION;

    strcpy(info->listening_origin, "btle://");

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unable to obtain netportal info");

    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtnetportal_btle_dispose(dtnetportal_btle_t* self)
{
    if (self == NULL)
        return;

    // TODO: Implement proper shutdown and cleanup in dtnetportal_btle_dispose().

    if (self->_is_malloced)
    {
        k_free(self);
    }
    else
    {
        memset(self, 0, sizeof(*self));
    }
}

// --------------------------------------------------------------------------------------------
// dtobject implementation
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Copy constructor
void
dtnetportal_btle_copy(dtnetportal_btle_t* this, dtnetportal_btle_t* that)
{
    // this object does not support copying
    (void)this;
    (void)that;
}

// --------------------------------------------------------------------------------------------
// Equality check
bool
dtnetportal_btle_equals(dtnetportal_btle_t* a, dtnetportal_btle_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    // TODO: Reconside equality semantics for dtnetportal_btle_equals backend.
    return (a->model_number == b->model_number);
}

// --------------------------------------------------------------------------------------------
const char*
dtnetportal_btle_get_class(dtnetportal_btle_t* self)
{
    return "dtnetportal_btle_t";
}

// --------------------------------------------------------------------------------------------

bool
dtnetportal_btle_is_iface(dtnetportal_btle_t* self, const char* iface_name)
{
    return strcmp(iface_name, DTNETPORTAL_IFACE_NAME) == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
// Convert to string
void
dtnetportal_btle_to_string(dtnetportal_btle_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    strncpy(buffer, "dtnetportal_btle_t", buffer_size);
    buffer[buffer_size - 1] = '\0';
}
