/*
 * dtnetportal_coap -- Zephyr CoAP/OpenThread network portal backend for the dtnetportal interface.
 *
 * Implements the dtnetportal vtable using CoAP over an OpenThread network.
 * An OpenThread dataset (supplied as a hex TLV string) configures the Thread
 * network at activation. Publish target, client identity, TLS certificate
 * paths, and a local CoAP listen port are configurable. An error callback
 * delivers async transport failures to the application without polling, and
 * an IP address callback reports discovered addresses on the Thread interface.
 *
 * cdox v1.0.2
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <dtcore/dterr.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dttasker.h>

#include <dtmc_base/dtnetportal.h>

#define DTNETPORTAL_COAP_FLAVOR "dtnetportal_coap"
#define DTNETPORTAL_COAP_VERSION "1.0.1"

// called when any callback has seen an error
typedef void (*dtnetportal_coap_handle_error_cb)(void* context, dterr_t* dterr);

// callback for each ip address found
typedef void (*dtnetportal_coap_ipaddr_cb)(void* context, const char* ipaddr_string, const char* extra_info_string);

typedef struct dtnetportal_coap_config_t
{
    const char* dataset_tlvs_hex;

    const char* publish_to_host; // e.g. "localhost"
    int publish_to_port;         // e.g. 1883 (or 8883 for TLS)
    int keepalive;               // seconds, e.g. 30

    // Client identity (all optional)
    const char* client_id; // if NULL, coap picks one
    const char* username;  // optional
    const char* password;  // optional

    // Session behavior
    bool clean_start; // true => clean session on connect

    // TLS options
    bool use_tls;             // false to disable TLS
    const char* tls_ca_file;  // path to CA cert (PEM), optional if broker cert is otherwise trusted
    const char* tls_certfile; // client cert (PEM), optional
    const char* tls_keyfile;  // client key (PEM), optional

    // --- NEW: local listener configuration
    uint16_t listen_port; // default 5683

    dtnetportal_coap_handle_error_cb handle_error_cb;

} dtnetportal_coap_config_t;

// Handy defaults
#define DTNETPORTAL_COAP_CONFIG_DEFAULTS                                                                                       \
    { .host = "localhost",                                                                                                     \
        .port = 1883,                                                                                                          \
        .keepalive = 30,                                                                                                       \
        .client_id = NULL,                                                                                                     \
        .username = NULL,                                                                                                      \
        .password = NULL,                                                                                                      \
        .clean_start = true,                                                                                                   \
        .use_tls = false,                                                                                                      \
        .tls_ca_file = NULL,                                                                                                   \
        .tls_certfile = NULL,                                                                                                  \
        .tls_keyfile = NULL }

typedef struct dtnetportal_coap_t dtnetportal_coap_t;

extern dterr_t*
dtnetportal_coap_create(dtnetportal_coap_t** self_ptr);

extern dterr_t*
dtnetportal_coap_init(dtnetportal_coap_t* self);

extern dterr_t*
dtnetportal_coap_configure(dtnetportal_coap_t* self, dtnetportal_coap_config_t* config);

// --------------------------------------------------------------------------------------
// Interface plumbing.

DTNETPORTAL_DECLARE_API(dtnetportal_coap);
DTOBJECT_DECLARE_API(dtnetportal_coap);
