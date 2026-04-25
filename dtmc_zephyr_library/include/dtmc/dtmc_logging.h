/*
 * dtmc_logging -- TAG-prefixed logging macros over the Zephyr LOG subsystem.
 *
 * Provides dtlog_info, dtlog_debug, and dtlog_error macros that prepend a
 * module tag name to every message and forward to the corresponding Zephyr
 * LOG_INF / LOG_DBG / LOG_ERR calls. A single DTMC_LOGGING(tagname) macro
 * both registers the Zephyr log module at DBG level and defines the static
 * TAG string, so each translation unit needs only one declaration line.
 *
 * cdox v1.0.2
 */
#pragma once
#include <zephyr/logging/log.h>

#define DTMC_LOGGING(tagname)                                                                                                  \
    LOG_MODULE_REGISTER(tagname, LOG_LEVEL_DBG);                                                                               \
    static const char* const TAG = #tagname;

#define dtlog_info(fmt, ...) LOG_INF("[" TAG "] " fmt, ##__VA_ARGS__)
#define dtlog_debug(fmt, ...) LOG_DBG("[" TAG "] " fmt, ##__VA_ARGS__)
#define dtlog_error(fmt, ...) LOG_ERR("[" TAG "] " fmt, ##__VA_ARGS__)
