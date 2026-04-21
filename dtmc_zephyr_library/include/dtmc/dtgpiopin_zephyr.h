/*
 * dtgpiopin_zephyr -- Zephyr GPIO backend for the dtgpiopin cross-platform interface.
 *
 * Implements the dtgpiopin vtable for Zephyr targets, targeting nRF5340-class
 * SoCs with two 32-pin ports. A single global pin number encodes both port
 * and pin (port = pin / 32, local = pin % 32), avoiding multi-field port
 * selection in the config. Mode, pull, and drive strength follow the common
 * dtgpiopin enumerations, keeping application code portable across platforms.
 *
 * cdox v1.0.2
 */
#pragma once

#include <dtmc_base/dtgpiopin.h>

typedef struct dtgpiopin_zephyr_t dtgpiopin_zephyr_t;

typedef struct
{
    uint8_t pin_number;
    dtgpiopin_mode_t mode;
    dtgpiopin_pull_t pull;
    dtgpiopin_drive_t drive;
} dtgpiopin_zephyr_config_t;

dterr_t*
dtgpiopin_zephyr_create(dtgpiopin_zephyr_t** self_ptr);
dterr_t*
dtgpiopin_zephyr_init(dtgpiopin_zephyr_t* self);
dterr_t*
dtgpiopin_zephyr_configure(dtgpiopin_zephyr_t* self, const dtgpiopin_zephyr_config_t* config);

// nRF5340 specifics: 2 ports × 32 pins each
#ifndef DTGPIOPIN_ZEPHYR_MAX_PORTS
#define DTGPIOPIN_ZEPHYR_MAX_PORTS 2
#endif

#ifndef DTGPIOPIN_ZEPHYR_PINS_PER_PORT
#define DTGPIOPIN_ZEPHYR_PINS_PER_PORT 32
#endif

#ifndef DTGPIOPIN_ZEPHYR_MAX_GLOBAL_PINS
#define DTGPIOPIN_ZEPHYR_MAX_GLOBAL_PINS (DTGPIOPIN_ZEPHYR_MAX_PORTS * DTGPIOPIN_ZEPHYR_PINS_PER_PORT)
#endif

// -----------------------------------------------------------------------------
// Interface plumbing (dtgpiopin facade implementation).

DTGPIOPIN_DECLARE_API(dtgpiopin_zephyr);
