// dtgpiopin_zephyr.c — Zephyr backend for dtgpiopin on nRF5340
//
// Global numbering: port = pin_number / 32, local_pin = pin_number % 32.
// Works with gpio0 / gpio1 via DT_NODELABEL(gpio0/gpio1).
//
// prj.conf needs: CONFIG_GPIO=y

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtstr.h>

#include <dtmc_base/dtgpiopin.h>

#include <dtmc/dtgpiopin_zephyr.h>

#define TAG "dtgpiopin_zephyr"

// ------------------------- helpers: global→port/pin -------------------------

static inline uint8_t
_port_from_global(uint32_t global_pin)
{
    return (uint8_t)(global_pin / DTGPIOPIN_ZEPHYR_PINS_PER_PORT);
}

static inline uint8_t
_local_from_global(uint32_t global_pin)
{
    return (uint8_t)(global_pin % DTGPIOPIN_ZEPHYR_PINS_PER_PORT);
}

// ----------------------------- concrete object -----------------------------

struct dtgpiopin_zephyr_t
{
    DTGPIOPIN_COMMON_MEMBERS;
    bool _is_malloced;

    dtgpiopin_zephyr_config_t config; // uses global pin_number, mode, pull, drive
    const struct device* port_dev;    // bound Zephyr device for current port

    // ISR hookup
    dtgpiopin_isr_fn cb;
    void* caller_context;
    bool irq_enabled;
};

// ------------------------------- vtable glue -------------------------------

DTGPIOPIN_INIT_VTABLE(dtgpiopin_zephyr)

// -------------------------- per-port ISR plumbing --------------------------

struct port_state
{
    const struct device* dev;
    struct gpio_callback cbk;
    bool callback_added;
    dtgpiopin_zephyr_t* owner[DTGPIOPIN_ZEPHYR_PINS_PER_PORT]; // by local pin
};

static struct port_state s_ports[DTGPIOPIN_ZEPHYR_MAX_PORTS] = { 0 };

static void
_zephyr_port_isr(const struct device* port_dev, struct gpio_callback* cb, uint32_t pins);

static inline struct port_state*
_get_port_state(uint8_t port_index)
{
    if (port_index >= DTGPIOPIN_ZEPHYR_MAX_PORTS)
        return NULL;
    return &s_ports[port_index];
}

static inline const struct device*
_bind_port_device(uint8_t port_index)
{
    switch (port_index)
    {
        case 0:
            return DEVICE_DT_GET(DT_NODELABEL(gpio0));
        case 1:
            return DEVICE_DT_GET(DT_NODELABEL(gpio1));
        default:
            return NULL;
    }
}

// ------------------------------- flag maps ---------------------------------

static inline gpio_flags_t
_map_mode_flags(dtgpiopin_mode_t mode, dtgpiopin_drive_t drive)
{
    gpio_flags_t f = 0;
    if (mode == DTGPIOPIN_MODE_INPUT)
        f |= GPIO_INPUT;
    if (mode == DTGPIOPIN_MODE_OUTPUT)
        f |= GPIO_OUTPUT;
    if (mode == DTGPIOPIN_MODE_INOUT)
        f |= (GPIO_INPUT | GPIO_OUTPUT);
    if (drive == DTGPIOPIN_DRIVE_OPEN_DRAIN)
        f |= GPIO_OPEN_DRAIN;
    return f;
}

static inline gpio_flags_t
_map_pull_flags(dtgpiopin_pull_t pull)
{
    switch (pull)
    {
        case DTGPIOPIN_PULL_UP:
            return GPIO_PULL_UP;
        case DTGPIOPIN_PULL_DOWN:
            return GPIO_PULL_DOWN;
        default:
            return 0;
    }
}

// --------------------------- lifecycle / factory ---------------------------

dterr_t*
dtgpiopin_zephyr_create(dtgpiopin_zephyr_t** self_ptr)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self_ptr);

    *self_ptr = (dtgpiopin_zephyr_t*)malloc(sizeof(**self_ptr));
    if (!*self_ptr)
        return dterr_new(DTERR_NOMEM, DTERR_LOC, NULL, "malloc %zu", sizeof(**self_ptr));

    DTERR_C(dtgpiopin_zephyr_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;
cleanup:
    if (dterr)
    {
        free(*self_ptr);
        *self_ptr = NULL;
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtgpiopin_zephyr_create failed");
    }
    return dterr;
}

// ----------------------------------------------------------------------
// Initialize the Zephyr GPIO pin instance (without malloc)

dterr_t*
dtgpiopin_zephyr_init(dtgpiopin_zephyr_t* self)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    memset(self, 0, sizeof(*self));

    self->model_number = DTMC_BASE_CONSTANTS_GPIOPIN_MODEL_ZEPHYR;
    self->config.pin_number = 0; // global
    self->config.mode = DTGPIOPIN_MODE_INPUT;
    self->config.pull = DTGPIOPIN_PULL_NONE;
    self->config.drive = DTGPIOPIN_DRIVE_DEFAULT;

    DTERR_C(dtgpiopin_set_vtable(self->model_number, &dtgpiopin_zephyr_vt));
cleanup:
    if (dterr)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "dtgpiopin_zephyr_init failed");
    return dterr;
}

// ----------------------------------------------------------------------
// Configure the Zephyr GPIO pin instance
dterr_t*
dtgpiopin_zephyr_configure(dtgpiopin_zephyr_t* self, const dtgpiopin_zephyr_config_t* config)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(config);

    if (config->pin_number >= DTGPIOPIN_ZEPHYR_MAX_GLOBAL_PINS)
    {
        return dterr_new(DTERR_FAIL,
          DTERR_LOC,
          NULL,
          "global pin %u out of range [0..%u)",
          (unsigned)config->pin_number,
          (unsigned)DTGPIOPIN_ZEPHYR_MAX_GLOBAL_PINS);
    }

    self->config = *config;
cleanup:
    return dterr;
}

// ------------------------------- implementations ------------------------------
// Attach hardware, including actuating pin mode, pull, drive, etc.

dterr_t*
dtgpiopin_zephyr_attach(dtgpiopin_zephyr_t* self DTGPIOPIN_ATTACH_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    const uint8_t port_index = _port_from_global(self->config.pin_number);
    const uint8_t local_pin = _local_from_global(self->config.pin_number);

    struct port_state* ps = _get_port_state(port_index);
    if (!ps)
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "invalid port=%u", port_index);

    if (!ps->dev)
    {
        ps->dev = _bind_port_device(port_index);
        if (!ps->dev || !device_is_ready(ps->dev))
            return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "GPIO device not ready: gpio%u", port_index);
    }
    self->port_dev = ps->dev;

    const gpio_flags_t flags = _map_mode_flags(self->config.mode, self->config.drive) | _map_pull_flags(self->config.pull);

    int rc = gpio_pin_configure(self->port_dev, local_pin, flags);
    if (rc)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_pin_configure(%u): rc=%d", (unsigned)local_pin, rc);
        goto cleanup;
    }

    ps->owner[local_pin] = self;

    self->cb = cb;
    self->caller_context = caller_context;

cleanup:
    return dterr;
}

// ------------------------------- enable irq --------------------------------

dterr_t*
dtgpiopin_zephyr_enable(dtgpiopin_zephyr_t* self DTGPIOPIN_ENABLE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    const uint8_t port_index = _port_from_global(self->config.pin_number);
    const uint8_t local_pin = _local_from_global(self->config.pin_number);

    struct port_state* ps = _get_port_state(port_index);
    if (!ps || !ps->dev)
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "port not bound");

    int rc = gpio_pin_interrupt_configure(ps->dev, local_pin, enable ? GPIO_INT_EDGE_BOTH : GPIO_INT_DISABLE);
    if (rc)
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_pin_interrupt_configure rc=%d", rc);

    if (enable && !ps->callback_added)
    {
        gpio_init_callback(&ps->cbk, _zephyr_port_isr, BIT(local_pin));
        gpio_add_callback(ps->dev, &ps->cbk);
        ps->callback_added = true;
    }
    else if (enable)
    {
        ps->cbk.pin_mask |= BIT(local_pin);
    }
    else
    {
        ps->cbk.pin_mask &= ~BIT(local_pin);
    }

    self->irq_enabled = enable;
cleanup:
    return dterr;
}

// ---------------------------------- read -----------------------------------

dterr_t*
dtgpiopin_zephyr_read(dtgpiopin_zephyr_t* self DTGPIOPIN_READ_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_level);

    const uint8_t local_pin = _local_from_global(self->config.pin_number);

    int rc = gpio_pin_get(self->port_dev, local_pin);
    if (rc < 0)
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_pin_get rc=%d", rc);

    *out_level = (rc != 0);
cleanup:
    return dterr;
}

// ---------------------------------- write ----------------------------------

dterr_t*
dtgpiopin_zephyr_write(dtgpiopin_zephyr_t* self DTGPIOPIN_WRITE_ARGS)
{
    dterr_t* dterr = NULL;
    DTERR_ASSERT_NOT_NULL(self);

    if (!(self->config.mode == DTGPIOPIN_MODE_OUTPUT || self->config.mode == DTGPIOPIN_MODE_INOUT))
    {
        return dterr_new(
          DTERR_FAIL, DTERR_LOC, NULL, "write invalid in INPUT mode (pin=%u)", (unsigned)self->config.pin_number);
    }

    const uint8_t local_pin = _local_from_global(self->config.pin_number);

    int rc = gpio_pin_set(self->port_dev, local_pin, level ? 1 : 0);
    if (rc)
        return dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "gpio_pin_set rc=%d", rc);

cleanup:
    return dterr;
}

// -----------------------------------------------------------------------------
// Human-readable status string

dterr_t*
dtgpiopin_zephyr_concat_format(dtgpiopin_zephyr_t* self DTGPIOPIN_CONCAT_FORMAT_ARGS)
{
    dterr_t* dterr = NULL;
    char* s = NULL;
    DTERR_ASSERT_NOT_NULL(self);
    DTERR_ASSERT_NOT_NULL(out_str);

    const uint8_t port_index = _port_from_global(self->config.pin_number);
    const uint8_t local_pin = _local_from_global(self->config.pin_number);

    s = dtstr_concat_format(s, NULL, "gpiopin_zephyr (%" PRId32 ") port=%u pin=%u", self->model_number, port_index, local_pin);

    struct port_state* ps = _get_port_state(port_index);
    if (!ps)
    {
        s = dtstr_concat_format(s, NULL, " invalid port");
        goto cleanup;
    }

    if (!ps->dev)
    {
        ps->dev = _bind_port_device(port_index);
        if (!ps->dev || !device_is_ready(ps->dev))
        {
            s = dtstr_concat_format(s, NULL, " GPIO device not ready");
            goto cleanup;
        }
    }
    self->port_dev = ps->dev;

    gpio_flags_t existing_flags;
    int rc = gpio_pin_get_config(ps->dev, local_pin, &existing_flags);
    if (rc)
    {
        s = dtstr_concat_format(s, NULL, " gpio_pin_get_config rc=%d (%s)", rc, strerror(-rc));
        goto cleanup;
    }

    if (gpio_pin_is_input(ps->dev, local_pin))
    {
        s = dtstr_concat_format(s, NULL, " gpio_pin_is_input");
    }
    if (gpio_pin_is_output(ps->dev, local_pin))
    {
        s = dtstr_concat_format(s, NULL, " gpio_pin_is_output");
    }

    if (existing_flags & GPIO_OUTPUT)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_OUTPUT");
    }
    if (existing_flags & GPIO_INPUT)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_INPUT");
    }
    if (existing_flags & GPIO_PUSH_PULL)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_PUSH_PULL");
    }

    // drive flags
    if (existing_flags & GPIO_OPEN_DRAIN)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_OPEN_DRAIN");
    }
    if (existing_flags & GPIO_OPEN_SOURCE)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_OPEN_SOURCE");
    }
    if (existing_flags & GPIO_LINE_OPEN_DRAIN)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_LINE_OPEN_DRAIN");
    }
    if (existing_flags & GPIO_SINGLE_ENDED)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_SINGLE_ENDED");
    }

    // bias flags
    if (existing_flags & GPIO_PULL_UP)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_PULL_UP");
    }
    if (existing_flags & GPIO_PULL_DOWN)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_PULL_DOWN");
    }

    // active level flags
    if (existing_flags & GPIO_ACTIVE_LOW)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_ACTIVE_LOW");
    }
    if (existing_flags & GPIO_ACTIVE_HIGH)
    {
        s = dtstr_concat_format(s, NULL, " GPIO_ACTIVE_HIGH");
    }

cleanup:
    *out_str = s;
    return dterr;
}

// --------------------------------- dispose ---------------------------------

void
dtgpiopin_zephyr_dispose(dtgpiopin_zephyr_t* self)
{
    if (!self)
        return;

    const uint8_t port_index = _port_from_global(self->config.pin_number);
    const uint8_t local_pin = _local_from_global(self->config.pin_number);

    struct port_state* ps = _get_port_state(port_index);
    if (ps && ps->dev)
    {
        (void)gpio_pin_interrupt_configure(ps->dev, local_pin, GPIO_INT_DISABLE);
        if (ps->owner[local_pin] == self)
        {
            ps->owner[local_pin] = NULL;
            ps->cbk.pin_mask &= ~BIT(local_pin);
        }
    }

    self->cb = NULL;
    self->caller_context = NULL;
    self->irq_enabled = false;

    if (self->_is_malloced)
        free(self);
    else
        memset(self, 0, sizeof(*self));
}

// ---------------------------------- ISR ------------------------------------

static void
_zephyr_port_isr(const struct device* port_dev, struct gpio_callback* cb, uint32_t pins)
{
    // Find matching port_state by callback pointer
    for (uint8_t port_index = 0; port_index < DTGPIOPIN_ZEPHYR_MAX_PORTS; ++port_index)
    {
        struct port_state* ps = &s_ports[port_index];
        if (&ps->cbk != cb)
            continue;

        while (pins)
        {
            uint32_t bit = __builtin_ctz(pins);
            pins &= (pins - 1);

            if (bit >= DTGPIOPIN_ZEPHYR_PINS_PER_PORT)
                continue;

            dtgpiopin_zephyr_t* owner = ps->owner[bit];
            if (!owner || !owner->irq_enabled || !owner->cb)
                continue;

            int level = gpio_pin_get(ps->dev, (uint8_t)bit);
            if (level < 0)
                continue;

            owner->cb(level ? DTGPIOPIN_IRQ_RISING : DTGPIOPIN_IRQ_FALLING, owner->caller_context);
        }
        break;
    }
}
