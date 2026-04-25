// Zephyr backend for dtadc on nRF5340 (CPU sampling, no DMA).
// Simple, readable implementation:
// - one background task
// - one adc_read() per configured channel per scan
// - no async, no DMA, no multi-channel packed buffer tricks

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <hal/nrf_saadc.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtcore_helper.h>
#include <dtcore/dterr.h>
#include <dtcore/dtheaper.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtadc.h>
#include <dtmc_base/dtlock.h>
#include <dtmc_base/dtruntime.h>
#include <dtmc_base/dtsemaphore.h>
#include <dtmc_base/dttasker.h>

#include <dtmc/dtadc_zephyr_saadc.h>

#include <dtmc/dtmc.h>

#define TAG "dtadc_zephyr_saadc"
// #define dtlog_debug(TAG, ...)

// vtable
DTADC_INIT_VTABLE(dtadc_zephyr_saadc);

// Tunable defaults
#define DTADC_ZEPHYR_SAADC_READER_TASK_STACK (4096)
#define DTADC_ZEPHYR_SAADC_READER_TASK_PRIORITY (DTTASKER_PRIORITY_URGENT_MEDIUM)

// nRF SAADC has 8 logical channels
#define DTADC_ZEPHYR_SAADC_MAX_HW_CHANNELS (8)

typedef struct
{
    int32_t channel_id;               // SAADC logical channel number: 0..7
    nrf_saadc_input_t input_positive; // NRF_SAADC_INPUT_AIN0..AIN7 etc.
    enum adc_gain gain;
    enum adc_reference reference;
    uint16_t acquisition_time;
    uint8_t resolution;
    struct adc_channel_cfg channel_cfg;
} dtadc_zephyr_saadc_channel_runtime_t;

// -----------------------------------------------------------------------------
// Concrete type

typedef struct dtadc_zephyr_saadc_t
{
    DTADC_COMMON_MEMBERS

    dtadc_zephyr_saadc_config_t config;

    bool is_configured;
    bool is_active;

    // protects status and activation state
    dtlock_handle lock;

    // signal to background reader task to stop when deactivated
    dtsemaphore_handle reader_tasker_should_stop_semaphore;

    // high priority background task continuously reads scans from the hardware
    dttasker_handle reader_tasker_handle;

    dtadc_scan_callback_fn scan_callback_fn;
    void* scan_callback_context;

    // re-used scan payload
    dtadc_scan_t scan;

    // raw values, one per configured channel
    int32_t channels[DTADC_ZEPHYR_SAADC_MAX_CHANNELS];

    // resolved runtime descriptors
    dtadc_zephyr_saadc_channel_runtime_t channel_runtime[DTADC_ZEPHYR_SAADC_MAX_CHANNELS];

    const struct device* adc_dev;

    dtadc_status_t status;
} dtadc_zephyr_saadc_t;

static dterr_t*
dtadc_zephyr_saadc__reader_task_entry(void* arg, dttasker_handle tasker_handle);

static dterr_t*
dtadc_zephyr_saadc__validate_config(const dtadc_zephyr_saadc_config_t* cfg);

static dterr_t*
dtadc_zephyr_saadc__open_hardware(dtadc_zephyr_saadc_t* self);

static void
dtadc_zephyr_saadc__close_hardware(dtadc_zephyr_saadc_t* self);

static dterr_t*
dtadc_zephyr_saadc__resolve_channel(const dtadc_zephyr_saadc_channel_config_t* src, dtadc_zephyr_saadc_channel_runtime_t* dst);

static const char*
dtadc_zephyr_saadc__input_positive_to_string(nrf_saadc_input_t input_positive);

// -----------------------------------------------------------------------------
// Defaults

void
dtadc_zephyr_saadc_config_init_defaults(dtadc_zephyr_saadc_config_t* cfg)
{
    int32_t i;

    if (!cfg)
        return;

    memset(cfg, 0, sizeof(*cfg));
    cfg->scan_interval_ms = DTADC_ZEPHYR_SAADC_DEFAULT_SCAN_INTERVAL_MS;
    cfg->task_stack_size = DTADC_ZEPHYR_SAADC_READER_TASK_STACK;
    cfg->task_priority = DTADC_ZEPHYR_SAADC_READER_TASK_PRIORITY;

    // Pick 4 straightforward AIN inputs for a first demo backend.
    // Adjust these to match the actual board pins you wire on nRF5340.
    cfg->channel_count = 4;

    cfg->channels[0].channel_id = 0;
    cfg->channels[0].input_positive = NRF_SAADC_INPUT_AIN0;

    cfg->channels[1].channel_id = 1;
    cfg->channels[1].input_positive = NRF_SAADC_INPUT_AIN1;

    cfg->channels[2].channel_id = 2;
    cfg->channels[2].input_positive = NRF_SAADC_INPUT_AIN2;

    cfg->channels[3].channel_id = 3;
    cfg->channels[3].input_positive = NRF_SAADC_INPUT_AIN3;

    for (i = 0; i < cfg->channel_count; i++)
    {
        // Common Nordic SAADC-friendly defaults for 0..~3.6V-ish front ends
        // depending on actual source impedance and scaling.
        cfg->channels[i].gain = ADC_GAIN_1_6;
        cfg->channels[i].reference = ADC_REF_INTERNAL;
        cfg->channels[i].acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40);
        cfg->channels[i].resolution = 12;
    }
}

// -----------------------------------------------------------------------------
// Lifecycle

dterr_t*
dtadc_zephyr_saadc_create(dtadc_zephyr_saadc_t** self_ptr)
{
    dterr_t* dterr = NULL;
    dtadc_zephyr_saadc_t* self = NULL;

    DTERR_ASSERT_NOT_NULL(self_ptr);

    DTERR_C(dtheaper_alloc_and_zero(sizeof(dtadc_zephyr_saadc_t), "dtadc_zephyr_saadc_t", (void**)&self));

    *self_ptr = self;

    DTERR_C(dtadc_zephyr_saadc_init(self));

cleanup:
    if (dterr)
    {
        dtheaper_free(self);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_zephyr_saadc_create failed");
    }
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtadc_zephyr_saadc_init(dtadc_zephyr_saadc_t* self)
{
    dterr_t* dterr = NULL;
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);

    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_ADC_MODEL_ZEPHYR_SAADC;

    DTERR_C(dtadc_set_vtable(self->model_number, &dtadc_zephyr_saadc_vt));

    self->scan.channels = self->channels;

    for (i = 0; i < DTADC_ZEPHYR_SAADC_MAX_CHANNELS; i++)
        self->channel_runtime[i].channel_id = -1;

    DTERR_C(dtlock_create(&self->lock));

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_zephyr_saadc_init failed");
    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtadc_zephyr_saadc_configure(dtadc_zephyr_saadc_t* self, const dtadc_zephyr_saadc_config_t* config)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);

    if (self->is_active)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "cannot configure while active");
        goto cleanup;
    }

    DTERR_C(dtadc_zephyr_saadc__validate_config(config));

    self->config = *config;

    self->is_configured = true;

cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_zephyr_saadc_configure failed");
    return dterr;
}

// -----------------------------------------------------------------------------
// Vtable targets

dterr_t*
dtadc_zephyr_saadc_activate(dtadc_zephyr_saadc_t* self DTADC_ACTIVATE_ARGS)
{
    dterr_t* dterr = NULL;
    bool is_locked = false;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(scan_callback_fn);

    if (!self->is_configured)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtadc_zephyr_saadc must be configured before activate");
        goto cleanup;
    }

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;

    if (self->is_active)
    {
        dterr = dterr_new(DTERR_BADCONFIG, DTERR_LOC, NULL, "dtadc_zephyr_saadc already active");
        goto cleanup;
    }

    self->scan_callback_fn = scan_callback_fn;
    self->scan_callback_context = scan_callback_context;
    self->status.dterr = NULL;
    self->status.state = DTADC_STATE_STARTING;

    DTERR_C(dtadc_zephyr_saadc__open_hardware(self));
    DTERR_C(dtsemaphore_create(&self->reader_tasker_should_stop_semaphore, 0, 0));

    {
        dttasker_config_t c = { 0 };
        c.name = "dtadc";
        c.tasker_entry_point_fn = dtadc_zephyr_saadc__reader_task_entry;
        c.tasker_entry_point_arg = self;
        c.stack_size = self->config.task_stack_size > 0 ? self->config.task_stack_size : DTADC_ZEPHYR_SAADC_READER_TASK_STACK;
        c.priority = self->config.task_priority > 0 ? self->config.task_priority : DTADC_ZEPHYR_SAADC_READER_TASK_PRIORITY;
        c.tasker_info_callback = self->config.tasker_info_callback_fn;
        c.tasker_info_callback_context = self->config.tasker_info_callback_context;

        DTERR_C(dttasker_create(&self->reader_tasker_handle, &c));
    }

    DTERR_C(dttasker_start(self->reader_tasker_handle));

    self->is_active = true;

cleanup:
    if (is_locked)
        dtlock_release(self->lock);

    if (dterr)
    {
        dtadc_zephyr_saadc__close_hardware(self);
        dtsemaphore_dispose(self->reader_tasker_should_stop_semaphore);
        self->reader_tasker_should_stop_semaphore = NULL;
        dttasker_dispose(self->reader_tasker_handle);
        self->reader_tasker_handle = NULL;
        self->is_active = false;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_zephyr_saadc_activate failed");
    }

    return dterr;
}

// -----------------------------------------------------------------------------

dterr_t*
dtadc_zephyr_saadc_deactivate(dtadc_zephyr_saadc_t* self DTADC_DEACTIVATE_ARGS)
{
    dterr_t* dterr = NULL;
    bool is_locked = false;
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;

    if (!self->is_active)
        goto cleanup;

    if (self->reader_tasker_should_stop_semaphore)
        DTERR_C(dtsemaphore_post(self->reader_tasker_should_stop_semaphore));

    if (is_locked)
    {
        dtlock_release(self->lock);
        is_locked = false;
    }

    // crude but acceptable for a first backend
    for (i = 0; i < 50; i++)
    {
        dtruntime_sleep_milliseconds(10);

        DTERR_C(dtlock_acquire(self->lock));
        is_locked = true;

        if (self->status.state == DTADC_STATE_STOPPED || self->status.state == DTADC_STATE_ERROR)
            break;

        dtlock_release(self->lock);
        is_locked = false;
    }

    dttasker_dispose(self->reader_tasker_handle);
    self->reader_tasker_handle = NULL;

    dtsemaphore_dispose(self->reader_tasker_should_stop_semaphore);
    self->reader_tasker_should_stop_semaphore = NULL;

    dtadc_zephyr_saadc__close_hardware(self);

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;
    self->is_active = false;

cleanup:
    if (is_locked)
        dtlock_release(self->lock);

    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtadc_zephyr_saadc_deactivate failed");
    return dterr;
}

// -----------------------------------------------------------------------------

void
dtadc_zephyr_saadc_dispose(dtadc_zephyr_saadc_t* self)
{
    if (!self)
        return;

    if (self->is_active)
        dtadc_zephyr_saadc_deactivate(self);

    dtadc_zephyr_saadc__close_hardware(self);

    dttasker_dispose(self->reader_tasker_handle);
    self->reader_tasker_handle = NULL;

    dtsemaphore_dispose(self->reader_tasker_should_stop_semaphore);
    self->reader_tasker_should_stop_semaphore = NULL;

    dtlock_dispose(self->lock);
    self->lock = NULL;

    memset(self, 0, sizeof(*self));
    dtheaper_free(self);
}

// --------------------------------------------------------------------------------------------
// return status

dterr_t*
dtadc_zephyr_saadc_get_status(dtadc_zephyr_saadc_t* self DTADC_GET_STATUS_ARGS)
{
    dterr_t* dterr = NULL;
    bool is_locked = false;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(status);

    DTERR_C(dtlock_acquire(self->lock));
    is_locked = true;

    *status = self->status;

cleanup:
    if (is_locked)
        dtlock_release(self->lock);
    return dterr;
}

// --------------------------------------------------------------------------------------------
// Convert to string

dterr_t*
dtadc_zephyr_saadc_to_string(dtadc_zephyr_saadc_t* self, char* buffer, int32_t buffer_size)
{
    dterr_t* dterr = NULL;
    int32_t i;
    int32_t offset = 0;

    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(buffer);

    if (buffer_size <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "buffer_size must be > 0");
        goto cleanup;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "dtadc_zephyr_saadc");

    offset += snprintf(buffer + offset,
      buffer_size - offset,
      " nch=%" PRId32 " interval=%" PRId32 "ms",
      self->config.channel_count,
      self->config.scan_interval_ms);

    offset += snprintf(buffer + offset, buffer_size - offset, " [");

    for (i = 0; i < self->config.channel_count; i++)
    {
        if (i > 0)
            offset += snprintf(buffer + offset, buffer_size - offset, ", ");

        offset += snprintf(buffer + offset,
          buffer_size - offset,
          "CH%" PRId32 "->%s",
          self->config.channels[i].channel_id,
          dtadc_zephyr_saadc__input_positive_to_string(self->config.channels[i].input_positive));

        if (offset >= buffer_size)
            break;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "]");
    buffer[buffer_size - 1] = '\0';

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Reader task

static dterr_t*
dtadc_zephyr_saadc__reader_task_entry(void* arg, dttasker_handle tasker_handle)
{
    dterr_t* dterr = NULL;
    dtadc_zephyr_saadc_t* self = (dtadc_zephyr_saadc_t*)arg;
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);

    self->status.state = DTADC_STATE_ACTIVE;

    DTERR_C(dttasker_ready(tasker_handle));

    uint64_t sequence_number = 0;

    for (;;)
    {
        bool was_timeout = false;

        DTERR_C(dtsemaphore_wait(self->reader_tasker_should_stop_semaphore, self->config.scan_interval_ms, &was_timeout));
        if (!was_timeout)
            break;

        for (i = 0; i < self->config.channel_count; i++)
        {
            int16_t raw16 = 0;
            struct adc_sequence sequence;

            memset(&sequence, 0, sizeof(sequence));
            sequence.channels = BIT((uint32_t)self->channel_runtime[i].channel_id);
            sequence.buffer = &raw16;
            sequence.buffer_size = sizeof(raw16);
            sequence.resolution = self->channel_runtime[i].resolution;
            sequence.oversampling = 0;
            sequence.calibrate = false;

            if (adc_read(self->adc_dev, &sequence) != 0)
            {
                dterr = dterr_new(DTERR_STATE,
                  DTERR_LOC,
                  NULL,
                  "adc_read failed for channel_id=%d (%s)",
                  self->channel_runtime[i].channel_id,
                  dtadc_zephyr_saadc__input_positive_to_string(self->channel_runtime[i].input_positive));
                goto cleanup;
            }

            self->channels[i] = (int32_t)raw16;
        }

        self->scan.channels = self->channels;
        self->scan.timestamp_ns = dtruntime_now_milliseconds() * 1000000ULL;
        self->scan.sequence_number = sequence_number++;

        DTERR_C(self->scan_callback_fn(self->scan_callback_context, &self->scan));
    }

cleanup:
    if (self->lock)
        dtlock_acquire(self->lock);

    if (dterr)
    {
        dtlog_error(TAG, "reader task exiting with error: %s", dterr->message);
        if (self->status.dterr == NULL)
            self->status.dterr = dterr_new(dterr->error_code, DTERR_LOC, NULL, "reader task error");
        self->status.state = DTADC_STATE_ERROR;
    }
    else
    {
        dtlog_debug(TAG, "reader task exiting without error");
        self->status.state = DTADC_STATE_STOPPED;
    }

    if (self->lock)
        dtlock_release(self->lock);

    return dterr;
}

// ------------------------------------------------------------------
// Helpers

static dterr_t*
dtadc_zephyr_saadc__validate_config(const dtadc_zephyr_saadc_config_t* cfg)
{
    dterr_t* dterr = NULL;
    int32_t i;
    int32_t j;

    DTERR_ASSERT_NOT_NULL(cfg);

    if (cfg->scan_interval_ms <= 0)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "scan_interval_ms must be > 0");
        goto cleanup;
    }

    if (cfg->channel_count <= 0 || cfg->channel_count > DTADC_ZEPHYR_SAADC_MAX_CHANNELS)
    {
        dterr =
          dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "channel_count must be between 1 and %d", DTADC_ZEPHYR_SAADC_MAX_CHANNELS);
        goto cleanup;
    }

    for (i = 0; i < cfg->channel_count; i++)
    {
        if (cfg->channels[i].channel_id < 0 || cfg->channels[i].channel_id >= DTADC_ZEPHYR_SAADC_MAX_HW_CHANNELS)
        {
            dterr = dterr_new(DTERR_BADARG,
              DTERR_LOC,
              NULL,
              "channels[%d].channel_id must be between 0 and %d",
              i,
              DTADC_ZEPHYR_SAADC_MAX_HW_CHANNELS - 1);
            goto cleanup;
        }

        if (cfg->channels[i].resolution <= 0)
        {
            dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "channels[%d].resolution must be > 0", i);
            goto cleanup;
        }

        for (j = i + 1; j < cfg->channel_count; j++)
        {
            if (cfg->channels[i].channel_id == cfg->channels[j].channel_id)
            {
                dterr = dterr_new(
                  DTERR_BADARG, DTERR_LOC, NULL, "duplicate channel_id %d in channel list", cfg->channels[i].channel_id);
                goto cleanup;
            }
        }
    }

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

static dterr_t*
dtadc_zephyr_saadc__resolve_channel(const dtadc_zephyr_saadc_channel_config_t* src, dtadc_zephyr_saadc_channel_runtime_t* dst)
{
    dterr_t* dterr = NULL;

    DTERR_ASSERT_NOT_NULL(src);
    DTERR_ASSERT_NOT_NULL(dst);

    memset(dst, 0, sizeof(*dst));

    dst->channel_id = src->channel_id;
    dst->input_positive = src->input_positive;
    dst->gain = src->gain;
    dst->reference = src->reference;
    dst->acquisition_time = src->acquisition_time;
    dst->resolution = src->resolution;

    memset(&dst->channel_cfg, 0, sizeof(dst->channel_cfg));
    dst->channel_cfg.gain = src->gain;
    dst->channel_cfg.reference = src->reference;
    dst->channel_cfg.acquisition_time = src->acquisition_time;
    dst->channel_cfg.channel_id = (uint8_t)src->channel_id;
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
    dst->channel_cfg.input_positive = src->input_positive;
#else
    dst->channel_cfg.input_positive = src->input_positive;
#endif

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------

static dterr_t*
dtadc_zephyr_saadc__open_hardware(dtadc_zephyr_saadc_t* self)
{
    dterr_t* dterr = NULL;
    int32_t i;

    DTERR_ASSERT_NOT_NULL(self);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(adc), okay)
    self->adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
#else
    self->adc_dev = NULL;
#endif

    if (!self->adc_dev)
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "ADC device not found in devicetree");
        goto cleanup;
    }

    if (!device_is_ready(self->adc_dev))
    {
        dterr = dterr_new(DTERR_STATE, DTERR_LOC, NULL, "ADC device is not ready");
        goto cleanup;
    }

    memset(self->channel_runtime, 0, sizeof(self->channel_runtime));
    for (i = 0; i < DTADC_ZEPHYR_SAADC_MAX_CHANNELS; i++)
        self->channel_runtime[i].channel_id = -1;

    for (i = 0; i < self->config.channel_count; i++)
    {
        DTERR_C(dtadc_zephyr_saadc__resolve_channel(&self->config.channels[i], &self->channel_runtime[i]));

        if (adc_channel_setup(self->adc_dev, &self->channel_runtime[i].channel_cfg) != 0)
        {
            dterr = dterr_new(DTERR_STATE,
              DTERR_LOC,
              NULL,
              "adc_channel_setup failed for channel_id=%d (%s)",
              self->channel_runtime[i].channel_id,
              dtadc_zephyr_saadc__input_positive_to_string(self->channel_runtime[i].input_positive));
            goto cleanup;
        }
    }

cleanup:
    if (dterr)
        dtadc_zephyr_saadc__close_hardware(self);

    return dterr;
}

// -----------------------------------------------------------------------------

static void
dtadc_zephyr_saadc__close_hardware(dtadc_zephyr_saadc_t* self)
{
    if (!self)
        return;

    self->adc_dev = NULL;
}

// -----------------------------------------------------------------------------

static const char*
dtadc_zephyr_saadc__input_positive_to_string(nrf_saadc_input_t input_positive)
{
    switch (input_positive)
    {
        case NRF_SAADC_INPUT_AIN0:
            return "AIN0";
        case NRF_SAADC_INPUT_AIN1:
            return "AIN1";
        case NRF_SAADC_INPUT_AIN2:
            return "AIN2";
        case NRF_SAADC_INPUT_AIN3:
            return "AIN3";
        case NRF_SAADC_INPUT_AIN4:
            return "AIN4";
        case NRF_SAADC_INPUT_AIN5:
            return "AIN5";
        case NRF_SAADC_INPUT_AIN6:
            return "AIN6";
        case NRF_SAADC_INPUT_AIN7:
            return "AIN7";
        case NRF_SAADC_INPUT_VDD:
            return "VDD";
        default:
            return "?";
    }
}