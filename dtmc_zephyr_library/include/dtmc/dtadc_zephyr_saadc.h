/*
 * dtadc_zephyr_saadc -- Zephyr nRF SAADC backend for the dtadc interface.
 *
 * Implements the dtadc vtable using the Zephyr ADC driver against the nRF
 * Successive Approximation ADC (SAADC). Up to eight channels are configured
 * individually with channel ID, analog input pin, gain, reference, acquisition
 * time, and resolution. A background task scans all channels at a configurable
 * interval and delivers results via a per-scan callback. An optional tasker
 * info callback provides runtime task diagnostics.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <hal/nrf_saadc.h>
#include <zephyr/drivers/adc.h>

#include <dtmc_base/dtadc.h>
#include <dtmc_base/dtadc_scan.h>
#include <dtmc_base/dttasker.h>

#define DTADC_ZEPHYR_SAADC_MAX_CHANNELS (8)
#define DTADC_ZEPHYR_SAADC_DEFAULT_SCAN_INTERVAL_MS (100)

typedef struct dtadc_zephyr_saadc_channel_config_t
{
    int32_t channel_id;               // 0..7
    nrf_saadc_input_t input_positive; // NRF_SAADC_INPUT_AIN0..AIN7
    enum adc_gain gain;
    enum adc_reference reference;
    uint16_t acquisition_time; // ADC_ACQ_TIME(...)
    uint8_t resolution;        // e.g. 12
} dtadc_zephyr_saadc_channel_config_t;

typedef struct dtadc_zephyr_saadc_config_t
{
    int32_t scan_interval_ms;
    int32_t task_stack_size;
    int32_t task_priority;

    int32_t channel_count;
    dtadc_zephyr_saadc_channel_config_t channels[DTADC_ZEPHYR_SAADC_MAX_CHANNELS];

    dttasker_info_callback_t tasker_info_callback_fn;
    void* tasker_info_callback_context;

} dtadc_zephyr_saadc_config_t;

typedef struct dtadc_zephyr_saadc_t dtadc_zephyr_saadc_t;

void
dtadc_zephyr_saadc_config_init_defaults(dtadc_zephyr_saadc_config_t* cfg);

dterr_t*
dtadc_zephyr_saadc_create(dtadc_zephyr_saadc_t** self_ptr);

dterr_t*
dtadc_zephyr_saadc_init(dtadc_zephyr_saadc_t* self);

dterr_t*
dtadc_zephyr_saadc_configure(dtadc_zephyr_saadc_t* self, const dtadc_zephyr_saadc_config_t* config);

dterr_t*
dtadc_zephyr_saadc_activate(dtadc_zephyr_saadc_t* self DTADC_ACTIVATE_ARGS);

dterr_t*
dtadc_zephyr_saadc_deactivate(dtadc_zephyr_saadc_t* self DTADC_DEACTIVATE_ARGS);

void
dtadc_zephyr_saadc_dispose(dtadc_zephyr_saadc_t* self);

dterr_t*
dtadc_zephyr_saadc_get_status(dtadc_zephyr_saadc_t* self DTADC_GET_STATUS_ARGS);

dterr_t*
dtadc_zephyr_saadc_to_string(dtadc_zephyr_saadc_t* self, char* buffer, int32_t buffer_size);

// -----------------------------------------------------------------------------
// Interface plumbing (dtadc facade implementation).

DTADC_DECLARE_API(dtadc_zephyr_saadc);
