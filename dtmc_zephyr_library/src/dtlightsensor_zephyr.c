#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/tsl2591.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>

#include <dtmc_base/dtlightsensor.h>

#include <dtmc/dtlightsensor_zephyr.h>

#define TAG "dtlightsensor_zephyr"

DTLIGHTSENSOR_INIT_VTABLE(dtlightsensor_zephyr);

typedef struct dtlightsensor_zephyr_t
{
    DTLIGHTSENSOR_COMMON_MEMBERS;

    dtlightsensor_zephyr_config_t config;

    int sensor_channel;

    const struct device* dev;

    bool _is_malloced;

} dtlightsensor_zephyr_t;

// --------------------------------------------------------------------------------------
extern dterr_t*
dtlightsensor_zephyr_create(dtlightsensor_zephyr_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (dtlightsensor_zephyr_t*)malloc(sizeof(dtlightsensor_zephyr_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(DTERR_NOMEM,
          DTERR_LOC,
          NULL,
          "failed to allocate %zu bytes for dtlightsensor_zephyr_t",
          sizeof(dtlightsensor_zephyr_t));
        goto cleanup;
    }

    DTERR_C(dtlightsensor_zephyr_init(*self_ptr));

    (*self_ptr)->_is_malloced = true;

cleanup:

    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
        {
            free(*self_ptr);
        }

        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtlightsensor_zephyr_create failed");
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtlightsensor_zephyr_init(dtlightsensor_zephyr_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_SENSOR_MODEL_ZEPHYR;

    // set the vtable for this model number
    DTERR_C(dtlightsensor_set_vtable(self->model_number, &_sensor_vt));

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "dtlightsensor_zephyr_init failed");
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtlightsensor_zephyr_configure(dtlightsensor_zephyr_t* self, dtlightsensor_zephyr_config_t* config)
{
    dterr_t* dterr = NULL;
    if (self == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "self is NULL");
        goto cleanup;
    }

    self->config = *config;

    if (self->config.device_name == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "device_name is required");
        goto cleanup;
    }

    self->sensor_channel = SENSOR_CHAN_LIGHT;

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "unable to configure zephyr sensor");

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtlightsensor_zephyr_activate(dtlightsensor_zephyr_t* self DTLIGHTSENSOR_ACTIVATE_ARGS)
{
    dterr_t* dterr = NULL;

    self->dev = device_get_binding(self->config.device_name);

    if (!device_is_ready(self->dev))
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "device not ready");
        goto cleanup;
    }

    if (self->config.integration_time_milliseconds != 0)
    {
        struct sensor_value val;
        int rc;

        // set integration time
        val.val1 = self->config.integration_time_milliseconds;
        val.val2 = 0;
        rc = sensor_attr_set(self->dev, self->sensor_channel, SENSOR_ATTR_INTEGRATION_TIME, &val);
        if (rc)
        {
            dterr = dterr_new(DTERR_FAIL,
              DTERR_LOC,
              NULL,
              "sensor_attr_set SENSOR_ATTR_INTEGRATION_TIME %d had error %d",
              self->config.integration_time_milliseconds,
              rc);
            goto cleanup;
        }
    }

    if (self->config.gain != 0)
    {
        struct sensor_value val;
        int rc;

        if (self->config.gain == DTLIGHTSENSOR_ZEPHYR_GAIN_LOW)
            val.val1 = TSL2591_SENSOR_GAIN_LOW;
        else if (self->config.gain == DTLIGHTSENSOR_ZEPHYR_GAIN_MEDIUM)
            val.val1 = TSL2591_SENSOR_GAIN_MED;
        else if (self->config.gain == DTLIGHTSENSOR_ZEPHYR_GAIN_HIGH)
            val.val1 = TSL2591_SENSOR_GAIN_HIGH;
        else if (self->config.gain > DTLIGHTSENSOR_ZEPHYR_GAIN_MAX)
            val.val1 = TSL2591_SENSOR_GAIN_MAX;
        else
        {
            dterr = dterr_new(DTERR_BADARG,
              DTERR_LOC,
              NULL,
              "gain enum %d is out of range (%d to %d)",
              self->config.gain,
              DTLIGHTSENSOR_ZEPHYR_GAIN_LOW,
              DTLIGHTSENSOR_ZEPHYR_GAIN_MAX);
            goto cleanup;
        }

        // set gain
        val.val2 = 0;
        rc = sensor_attr_set(self->dev, self->sensor_channel, SENSOR_ATTR_GAIN_MODE, &val);
        if (rc)
        {
            dterr =
              dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "sensor_attr_set SENSOR_ATTR_GAIN %d had error %d", self->config.gain, rc);
            goto cleanup;
        }
    }

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "unable to activate \"%s\" device", self->config.device_name);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtlightsensor_zephyr_sample(dtlightsensor_zephyr_t* self DTLIGHTSENSOR_SAMPLE_ARGS)
{
    dterr_t* dterr = NULL;
    int rc = 0;
    struct sensor_value lux = { 0 };
    bool is_saturated = false;
    double value = 0.0;

    rc = sensor_sample_fetch(self->dev);
    if (rc == -EOVERFLOW)
    {
        is_saturated = true;
    }
    else if (rc)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "sensor_sample_fetch failed with errno %d", rc);
        goto cleanup;
    }

    if (is_saturated)
    {
        value = -1.0; // indicate saturation
    }
    else
    {
        rc = sensor_channel_get(self->dev, self->sensor_channel, &lux);
        if (rc)
        {
            dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "sensor_channel_get failed with errno %d", rc);
            goto cleanup;
        }

        value = sensor_value_to_double(&lux);
    }

    *sample = (int64_t)(value);

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, dterr, "unable to sample zephyr sensor");

    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtlightsensor_zephyr_dispose(dtlightsensor_zephyr_t* self)
{
    if (self == NULL)
        return;

    if (self->_is_malloced)
    {
        free(self);
    }
    else
    {
        memset(self, 0, sizeof(*self));
    }
}
