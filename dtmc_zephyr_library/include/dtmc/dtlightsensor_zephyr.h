/*
 * dtlightsensor_zephyr -- Zephyr sensor subsystem backend for the dtlightsensor interface.
 *
 * Implements the dtlightsensor vtable using the Zephyr sensor driver API,
 * identified by a device name string. Integration time and gain (four levels
 * from low to max) are set at configuration time, allowing sensitivity to be
 * tuned to the ambient environment. The activate/sample/dispose contract
 * matches other dtlightsensor backends, keeping application code portable
 * across platforms.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/sensor.h>

#include <dtcore/dterr.h>

#include <dtmc_base/dtlightsensor.h>

typedef enum
{
    DTLIGHTSENSOR_ZEPHYR_GAIN_LOW = 1,
    DTLIGHTSENSOR_ZEPHYR_GAIN_MEDIUM = 2,
    DTLIGHTSENSOR_ZEPHYR_GAIN_HIGH = 3,
    DTLIGHTSENSOR_ZEPHYR_GAIN_MAX = 4
} dtlightsensor_zephyr_gain_enum;

typedef struct dtlightsensor_zephyr_config_t
{
    char* device_name;
    int integration_time_milliseconds;
    dtlightsensor_zephyr_gain_enum gain;

} dtlightsensor_zephyr_config_t;

typedef struct dtlightsensor_zephyr_t dtlightsensor_zephyr_t;

extern dterr_t*
dtlightsensor_zephyr_create(dtlightsensor_zephyr_t** self_ptr);

extern dterr_t*
dtlightsensor_zephyr_init(dtlightsensor_zephyr_t* self);

extern dterr_t*
dtlightsensor_zephyr_configure(dtlightsensor_zephyr_t* self, dtlightsensor_zephyr_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTLIGHTSENSOR_DECLARE_API(dtlightsensor_zephyr);
