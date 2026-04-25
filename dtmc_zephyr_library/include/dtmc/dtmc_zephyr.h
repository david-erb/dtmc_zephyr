/*
 * dtmc_zephyr -- Zephyr platform entry point for the dtmc embedded framework.
 *
 * Declares the platform flavor and version strings and a shell
 * initializer that registers dtmc diagnostic commands with the Zephyr
 * shell subsystem. Provides the common dtmc_flavor and dtmc_version
 * accessors used by other modules to report their build identity at
 * runtime.
 *
 * cdox v1.0.2
 */
#pragma once
#include <zephyr/shell/shell.h>

#include <dtmc/version.h>
#include <dtcore/dterr.h>

#define DTMC_ZEPHYR_FLAVOR "dtmc_zephyr"

const char*
dtmc_flavor(void);

const char*
dtmc_version(void);

extern dterr_t*
dtmc_zephyr_shell_initialize(void);
