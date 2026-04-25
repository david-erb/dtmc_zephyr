#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/net/openthread.h> // openthread_start(), get_default_*

#include <openthread/coap.h>
#include <openthread/dns.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtmc_base/dtbufferqueue.h>
#include <dtmc_base/dtmanifold.h>
#include <dtmc_base/dtmc_base_constants.h>
#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc/dtbufferqueue_zephyr.h>
#include <dtmc/dtnetportal_coap.h>

#define DTNETPORTAL_COAP_VERSION_MAJOR 0
#define DTNETPORTAL_COAP_VERSION_MINOR 2
#define DTNETPORTAL_COAP_VERSION_STRING #DTNETPORTAL_COAP_VERSION_MAJOR "." #DTNETPORTAL_COAP_VERSION_MINOR

// --------------------------------------------------------------------------------------
// private structure used internally
typedef struct dtnetportal_coap_t
{
    DTNETPORTAL_COMMON_MEMBERS;

    dtnetportal_coap_config_t config;

    dtsemaphore_handle manifold_semaphore;
    dtmanifold_t _manifold, *manifold;

    otInstance* ot_instance;
    otCoapResource _ot_coap_resource, *ot_coap_resource;

    char mleid_ipaddr_string[OT_IP6_ADDRESS_STRING_SIZE];

    char srp_hostname[64];
    char srp_instance_name[64];

    otDnsTxtEntry srp_dns_text_entry[1];
    otSrpClientService srp_service;
    char srp_service_key[32];
    char srp_service_val[64];

    bool _is_connected;
    bool _is_malloced;
} dtnetportal_coap_t;

// ---- helper methods
extern const char*
dtnetportal_coap__err_str(otError err);
extern const char*
dtnetportal_coap__role_str(otDeviceRole role);
extern dterr_t*
dtnetportal_coap__start_thread(dtnetportal_coap_t* self, uint32_t attach_timeout_ms);
extern void
dtnetportal_coap__each_ipaddr(dtnetportal_coap_ipaddr_cb callback_func, void* callback_context);
dterr_t*
dtnetportal_coap__srp_register(dtnetportal_coap_t* self);

// ---- openthread callbacks
extern void
dtnetportal_coap__handle_ingress(void* aContext, otMessage* aMessage, const otMessageInfo* aMessageInfo);
extern void
dtnetportal_coap__handle_response_to_publish(void* aContext, otMessage* aMessage, const otMessageInfo* aInfo, otError aResult);
extern void
dtnetportal_coap__srp_state_cb(otError error,
  const otSrpClientHostInfo* host,
  const otSrpClientService* services,
  const otSrpClientService* removed,
  void* ctx);