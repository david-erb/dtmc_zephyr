/*
 * dtmc -- Canonical top-level include for the Zephyr platform library.
 *
 * Re-exports the full dtmc_zephyr interface under the platform-agnostic
 * name <dtmc/dtmc.h>. Application code includes this header to remain
 * decoupled from the underlying platform selection while still accessing
 * the flavor string, version string, and Zephyr shell initializer
 * provided by dtmc_zephyr.
 *
 * cdox v1.0.2
 */
#pragma once
#include <dtmc/dtmc_zephyr.h>