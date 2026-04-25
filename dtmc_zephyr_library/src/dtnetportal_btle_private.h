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

#include <dtmc_base/dtmanifold.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

#include <dtmc/dtnetportal_btle.h>
#include <dtmc_base/dtnetportal.h>

typedef struct topic_to_attr_map_t
{
    const char* topic;
    const struct bt_gatt_attr* attr;
} topic_to_attr_map_t;

// --------------------------------------------------------------------------------------
typedef struct dtnetportal_btle_t
{
    DTNETPORTAL_COMMON_MEMBERS;

    dtnetportal_btle_config_t config;

    dtbuffer_t* pending;

    // used when we self-receive
    dtsemaphore_handle receive_semaphore_handle;

    struct k_sem bt_init_ok;

    int bt_enable_err;
    dterr_t* dterr_pile;

    struct bt_conn* conn;

#define MAX_ATTRIBUTES 5
    struct bt_conn_cb connection_callbacks;
    struct bt_gatt_cb gatt_callbacks;

    struct bt_data ad[2];
    struct bt_data sd[1];

    dttasker_handle advertiser_tasker; // tasker handle for the avertiser task
    struct k_sem advertiser_restart_semaphore;

    bool _is_malloced;

#define MAX_CHARACTERISTICS 10
    topic_to_attr_map_t topic_to_attr_map[MAX_CHARACTERISTICS];
    int topic_to_attr_map_count;

    dtmanifold_t _manifold, *manifold; // manifold for managing topics

} dtnetportal_btle_t;

extern dtnetportal_btle_t* dtnetportal_btle_global;

// --------------------------------------------------------------------------------------
extern dterr_t*
dtnetportal_btle_add_attribute(dtnetportal_btle_t* self, struct bt_gatt_attr* attr);

extern dterr_t*
dtnetportal_btle_start_advertising(dtnetportal_btle_t* self);

extern dterr_t*
dtnetportal_btle_advertiser_task_entrypoint(void* self_arg, dttasker_handle tasker_handle);

// --------------------------------------------------------------------------------------

extern void
dtnetportal_btle_btenabled_callback(int err);

extern void
dtnetportal_btle_connected_callback(struct bt_conn* conn, uint8_t err);

extern void
dtnetportal_btle_mtu_updated_callback(struct bt_conn* conn, uint16_t tx, uint16_t rx);

extern void
dtnetportal_btle_disconnected_callback(struct bt_conn* conn, uint8_t reason);

extern void
dtnetportal_btle_recycled_callback(void);

extern void
dtnetportal_btle_ccc_cfg_changed_callback(const struct bt_gatt_attr* attr, uint16_t value);

extern ssize_t
dtnetportal_btle_read_callback( //
  struct bt_conn* conn,
  const struct bt_gatt_attr* attr,
  void* buf,
  uint16_t len,
  uint16_t offset);

extern ssize_t
dtnetportal_btle_write_callback( //
  struct bt_conn* conn,
  const struct bt_gatt_attr* attr,
  const void* buf,
  uint16_t len,
  uint16_t offset,
  uint8_t flags);
