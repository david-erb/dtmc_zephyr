#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
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

#define TAG "dtiox_zephyr_canbus"

#define dtiox_zephyr_canbus_DEFAULT_RX_RING_CAPACITY 1024

// Hard-coded CAN settings for now.
// - Device: chosen CAN controller (MCP2515 over SPI on nRF5340 DK)
//   You must set this in your DTS:
//
//   / {
//       chosen {
//           zephyr,canbus = &mcp2515;
//       };
//   };
//
#define dtiox_zephyr_canbus_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))

// Bitrate in bit/s
#define dtiox_zephyr_canbus_BITRATE 500000U

typedef struct dtiox_zephyr_canbus_stats_t
{
    int32_t rx_callback_bytes;
    int32_t rx_callback_calls;
    int32_t rx_bytes;
    int32_t rx_calls;
    int32_t tx_bytes;
    int32_t tx_calls;
} dtiox_zephyr_canbus_stats_t;

// Concrete type
typedef struct dtiox_zephyr_canbus_t
{
    DTIOX_COMMON_MEMBERS
    bool _is_malloced;

    dtiox_zephyr_canbus_config_t config;

    // Zephyr CAN device
    const struct device* can_dev;
    bool dev_ready;
    bool dev_started;

    // RX FIFO: callback producer, foreground consumer
    dtringfifo_t rx_fifo;
    dtbuffer_t* rx_fifo_buffer;

    // Optional semaphore to wake user code (not used from ISR here)
    dtsemaphore_handle rx_semaphore;

    // CAN rx filter ID (catch-all)
    bool rx_filter_is_installed;
    int rx_filter_id;

    // Control whether incoming bytes are pushed into FIFO
    bool rx_enabled;

    // Overflow handling: set when FIFO write fails; reported on next read.
    bool rx_overflow_flag;
    int32_t fifo_dropped_bytes;

    // RX/TX counters (bytes + calls), clamped to INT32_MAX
    dtiox_zephyr_canbus_stats_t stats;

    // Error count (any dterr returned to caller), clamped to INT32_MAX
    int32_t rx_error_count;
    int32_t tx_error_count;

    int32_t tx_retry_milliseconds;
    int32_t tx_retry_count;
    int last_tx_error;

    // CAN state and error counters
    enum can_state cur_state;
    struct can_bus_err_cnt last_err_cnt;
    int32_t state_change_count;

} dtiox_zephyr_canbus_t;

// -----------------------------------------------------------------------------
// Internal helpers

extern int32_t
dtiox_zephyr_canbus__rx_fifo_capacity_from_config(const dtiox_zephyr_canbus_t* self);

// -----------------------------------------------------------------------------
// Push bytes into FIFO (callback producer)

extern int32_t
dtiox_zephyr_canbus__rx_fifo_push(dtiox_zephyr_canbus_t* self, const uint8_t* src, int32_t len);

// -----------------------------------------------------------------------------
// Pop bytes from FIFO (foreground consumer)

extern int32_t
dtiox_zephyr_canbus__rx_fifo_pop(dtiox_zephyr_canbus_t* self, uint8_t* dest, int32_t len);

// -----------------------------------------------------------------------------
// CAN state helpers

extern const char*
dtiox_zephyr_canbus__state_to_str(enum can_state state);

// -----------------------------------------------------------------------------
// Check that error state is valid to continue read or write operations
// - Fails if CAN controller is not ERROR_ACTIVE.
// - Also fails if error counters show any TX/RX errors have occurred.

extern dterr_t*
dtiox_zephyr_canbus__check_error_state(dtiox_zephyr_canbus_t* self);

// -----------------------------------------------------------------------------
// CAN callbacks

extern void
dtiox_zephyr_canbus__rx_callback( //
  const struct device* dev,       //
  struct can_frame* frame,        //
  void* user_data);

extern void
dtiox_zephyr_canbus__tx_callback( //
  const struct device* dev,       //
  int error,                      //
  void* user_data);

extern void
dtiox_zephyr_canbus__state_change_callback( //
  const struct device* dev,                 //
  enum can_state state,                     //
  struct can_bus_err_cnt err_cnt,           //
  void* user_data);