// POSIX dtnetportal over MQTT using libcoap, with dtmanifold fanout.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // read, write, pipe, close

#include <zephyr/net/openthread.h> // openthread_start(), get_default_*

#include <openthread/coap.h>
#include <openthread/dns.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/logging.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>

#include <dtmc_base/dtmc_base_constants.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtobject.h>

#include <dtmc_base/dtmanifold.h>
#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtsemaphore.h>

#include <dtmc/dtnetportal_coap.h>

#include "dtnetportal_coap_private.h"

DTNETPORTAL_INIT_VTABLE(dtnetportal_coap);
DTOBJECT_INIT_VTABLE(dtnetportal_coap);

#define TAG "dtnetportal_coap"

#ifdef COAP_EPOLL_SUPPORT
#error "This build of libcoap has epoll support enabled; use the epoll code path or rebuild without epoll."
#endif

// --------------------------------------------------------------------------------------
extern dterr_t*
dtnetportal_coap_create(dtnetportal_coap_t** self_ptr)
{
    dterr_t* dterr = NULL;

    *self_ptr = (dtnetportal_coap_t*)malloc(sizeof(dtnetportal_coap_t));
    if (*self_ptr == NULL)
    {
        dterr = dterr_new(
          DTERR_NOMEM, DTERR_LOC, NULL, "failed to allocate %zu bytes for dtnetportal_coap_t", sizeof(dtnetportal_coap_t));
        goto cleanup;
    }

    DTERR_C(dtnetportal_coap_init(*self_ptr));
    (*self_ptr)->_is_malloced = true;

cleanup:
    if (dterr != NULL)
    {
        if (*self_ptr != NULL)
            free(*self_ptr);
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "%s(): failed", __func__);
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_coap_init(dtnetportal_coap_t* self)
{
    dterr_t* dterr = NULL;

    memset(self, 0, sizeof(*self));
    self->model_number = DTMC_BASE_CONSTANTS_NETPORTAL_MODEL_COAP;

    DTERR_C(dtnetportal_set_vtable(self->model_number, &dtnetportal_coap_vt));
    DTERR_C(dtobject_set_vtable(self->model_number, &_object_vt));

    self->manifold = &self->_manifold;
    DTERR_C(dtmanifold_init(self->manifold));

    // create a semaphore for the manifold so it self-serializes
    {
        DTERR_C(dtsemaphore_create(&self->manifold_semaphore, 1, 1));
        DTERR_C(dtmanifold_set_threadsafe_semaphore(self->manifold, (dtsemaphore_handle)self->manifold_semaphore, 10));
    }

    // CoAP resource configuration
    self->ot_coap_resource = &self->_ot_coap_resource;
    self->ot_coap_resource->mUriPath = "ingress";
    self->ot_coap_resource->mHandler = dtnetportal_coap__handle_ingress;
    self->ot_coap_resource->mContext = self;
    self->ot_coap_resource->mNext = NULL;

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "%s(): failed", __func__);
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_coap_configure(dtnetportal_coap_t* self, dtnetportal_coap_config_t* config)
{
    dterr_t* dterr = NULL;
    if (config == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "called with NULL config");
        goto cleanup;
    }

    if (config->publish_to_host == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "publish_to_host is required");
        goto cleanup;
    }

    if (config->publish_to_port == 0)
        config->publish_to_port = 5683; // default CoAP port

    if (config->listen_port == 0)
        config->listen_port = 5683; // default CoAP port

    self->config = *config;

cleanup:
    return dterr;
}

// --------------------------------------------------------------------------------------------
static void
each_ipaddr_print(void* callback_context, const char* ip, const char* info)
{
    dtlog_debug(TAG, "    %-48s  [%s]", ip, info);
}

// --------------------------------------------------------------------------------------
// establish connection to the network
dterr_t*
dtnetportal_coap_activate(dtnetportal_coap_t* self DTNETPORTAL_ACTIVATE_ARGS)
{
    dterr_t* dterr = NULL;
    bool is_locked = false;

    otLoggingSetLevel(OT_LOG_LEVEL_DEBG); // allow to set at runtime with

    // get the openthread instance and start thread, waiting for a good role
    DTERR_C(dtnetportal_coap__start_thread(self, 60000));

    openthread_mutex_lock();
    is_locked = true;

    // grab the primary ipaddress (mesh-local EID)
    const otIp6Address* mleid = otThreadGetMeshLocalEid(self->ot_instance);
    otIp6AddressToString(mleid, self->mleid_ipaddr_string, sizeof self->mleid_ipaddr_string);

    otError coap_err;
    coap_err = otCoapStart(self->ot_instance, self->config.listen_port);
    if (coap_err != OT_ERROR_NONE)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "CoAP start failed");
        goto cleanup;
    }

    otCoapAddResource(self->ot_instance, self->ot_coap_resource);

    DTERR_C(dtnetportal_coap__srp_register(self));

    // print the ip addresses we have
    dtnetportal_coap__each_ipaddr(each_ipaddr_print, NULL);

cleanup:
    if (is_locked)
        openthread_mutex_unlock();

    if (dterr != NULL)
    {
        dterr = dterr_new(
          dterr->error_code, DTERR_LOC, dterr, "unable to activate netportal for listen port %d", self->config.listen_port);
    }
    return dterr;
}

// --------------------------------------------------------------------------------------
// subscribe must happen before activate
dterr_t*
dtnetportal_coap_subscribe(dtnetportal_coap_t* self DTNETPORTAL_SUBSCRIBE_ARGS)
{
    dterr_t* dterr = NULL;

    DTERR_C(dtmanifold_subscribe(self->manifold, topic, recipient_self, receive_callback));

    dtlog_debug(TAG, "%s(): subscribed to topic \"%s\"", __func__, topic);

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unable to subscribe to topic \"%s\"", topic);
    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_coap_publish(dtnetportal_coap_t* self DTNETPORTAL_PUBLISH_ARGS)
{
    dterr_t* dterr = NULL;
    otError error;
    otMessage* msg = NULL;
    otMessageInfo info;

    openthread_mutex_lock();

    // Create message buffer
    msg = otCoapNewMessage(self->ot_instance, /*settings*/ NULL);
    if (!msg)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "otCoapNewMessage: no memory");
        goto cleanup;
    }

    // Init as Confirmable POST with a small token
    otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);

    // Generate a token so response can be matched
    otCoapMessageGenerateToken(msg, 2);

    // Uri-Path: /ingress
    error = otCoapMessageAppendUriPathOptions(msg, "ingress");
    if (error != OT_ERROR_NONE)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "append path otError %d (%s)", error, dtnetportal_coap__err_str(error));
        goto cleanup;
    }

    error = otCoapMessageAppendUriQueryOption(msg, topic);
    if (error != OT_ERROR_NONE)
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "append query otError %d (%s)", error, dtnetportal_coap__err_str(error));
        goto cleanup;
    }

    // Payload marker
    error = otCoapMessageSetPayloadMarker(msg);
    if (error != OT_ERROR_NONE)
    {
        dterr =
          dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "payload marker otError %d (%s)", error, dtnetportal_coap__err_str(error));
        goto cleanup;
    }

    // Payload
    error = otMessageAppend(msg, buffer->payload, buffer->length);
    if (error != OT_ERROR_NONE)
    {
        dterr =
          dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "append payload otError %d (%s)", error, dtnetportal_coap__err_str(error));
        goto cleanup;
    }

    // Destination
    memset(&info, 0, sizeof(info));
    const char* publish_to_host = self->config.publish_to_host;
    if (!strcmp(publish_to_host, "") ||        //
        !strcmp(publish_to_host, "::") ||      //
        !strcmp(publish_to_host, "::1") ||     //
        !strcmp(publish_to_host, "localhost")) //
    {
        publish_to_host = self->mleid_ipaddr_string;
    }
    error = otIp6AddressFromString(publish_to_host, &info.mPeerAddr);
    if (error != OT_ERROR_NONE)
    {
        dterr = dterr_new(DTERR_FAIL,
          DTERR_LOC,
          NULL,
          "publish_to_host \"%s\" parse otError %d (%s)",
          self->config.publish_to_host,
          error,
          dtnetportal_coap__err_str(error));
        goto cleanup;
    }

    info.mPeerPort = self->config.publish_to_port;

    // Send, don't care about any response
    error = otCoapSendRequest(self->ot_instance, msg, &info, NULL, NULL);
    // error = otCoapSendRequest(self->ot_instance, msg, &info, dtnetportal_coap__handle_response_to_publish, self);
    if (error != OT_ERROR_NONE)
    {
        dterr =
          dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "otCoapSendRequest otError %d (%s)", error, dtnetportal_coap__err_str(error));
        goto cleanup;
    }

cleanup:
    if (dterr != NULL && msg != NULL)
        otMessageFree(msg);

    openthread_mutex_unlock();

    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unable to publish to topic \"%s\"", topic);

    return dterr;
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_coap_get_info(dtnetportal_coap_t* self, dtnetportal_info_t* info)
{
    dterr_t* dterr = NULL;
    if (info == NULL)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "called with NULL info");
        goto cleanup;
    }

    memset(info, 0, sizeof(*info));

    info->flavor = DTNETPORTAL_COAP_FLAVOR;
    info->version = DTNETPORTAL_COAP_VERSION;

    int needed = snprintf(info->listening_origin,
      sizeof(info->listening_origin),
      "coap://[%s]:%d",
      self->mleid_ipaddr_string,
      self->config.listen_port);

    if (needed >= sizeof(info->listening_origin))
    {
        dterr = dterr_new(DTERR_OVERFLOW, DTERR_LOC, NULL, "listening_origin buffer too small (%d needed)", needed + 1);
        goto cleanup;
    }

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unable to obtain netportal info");

    return dterr;
}

// --------------------------------------------------------------------------------------
void
dtnetportal_coap_dispose(dtnetportal_coap_t* self)
{
    if (self == NULL)
        return;

    if (self->ot_instance)
    {
        int rc;
        if ((rc = openthread_mutex_try_lock()) == 0)
        {
            if (self->ot_coap_resource)
            {
                otCoapRemoveResource(self->ot_instance, self->ot_coap_resource);
                self->ot_coap_resource = NULL;
            }

            otCoapSetDefaultHandler(self->ot_instance, NULL, NULL);

            otCoapStop(self->ot_instance);

            otThreadSetEnabled(self->ot_instance, false);

            openthread_mutex_unlock();

            self->ot_instance = NULL;
        }
        else
        {
            dtlog_error(TAG, "%s(): failed to lock openthread mutex, rc %d", __func__, rc);
        }
    }

    dtmanifold_dispose(self->manifold);
    dtsemaphore_dispose(self->manifold_semaphore);

    if (self->_is_malloced)
        free(self);
    else
        memset(self, 0, sizeof(*self));
}

// --------------------------------------------------------------------------------------------
// dtobject implementation
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Copy constructor
void
dtnetportal_coap_copy(dtnetportal_coap_t* this, dtnetportal_coap_t* that)
{
    // this object does not support copying
    (void)this;
    (void)that;
}

// --------------------------------------------------------------------------------------------
// Equality check
bool
dtnetportal_coap_equals(dtnetportal_coap_t* a, dtnetportal_coap_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    // TODO: Reconside equality semantics for dtnetportal_coap_equals backend.
    return (a->model_number == b->model_number);
}

// --------------------------------------------------------------------------------------------
const char*
dtnetportal_coap_get_class(dtnetportal_coap_t* self)
{
    return "dtnetportal_coap_t";
}

// --------------------------------------------------------------------------------------------

bool
dtnetportal_coap_is_iface(dtnetportal_coap_t* self, const char* iface_name)
{
    return strcmp(iface_name, DTNETPORTAL_IFACE_NAME) == 0 || //
           strcmp(iface_name, "dtobject_iface") == 0;
}

// --------------------------------------------------------------------------------------------
// Convert to string
void
dtnetportal_coap_to_string(dtnetportal_coap_t* self, char* buffer, size_t buffer_size)
{
    if (self == NULL || buffer == NULL || buffer_size == 0)
        return;

    strncpy(buffer, "dtnetportal_coap_t", buffer_size);
    buffer[buffer_size - 1] = '\0';
}
