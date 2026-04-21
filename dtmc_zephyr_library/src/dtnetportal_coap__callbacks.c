// callbacks from openthread CoAP server

#include <stdbool.h>
#include <string.h>

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
#include <dtmc_base/dtmanifold.h>
#include <dtmc_base/dtnetportal.h>

#include <dtmc/dtnetportal_coap.h>

#include "dtnetportal_coap_private.h"

#define TAG "dtnetportal_coap__callbacks"

#define dtlog_debug(TAG, ...)

/* --- CoAP POST "/ingress?topic" handler ----------------------------------- */
// note we don't check for either POST or the path to be actually ingress
void
dtnetportal_coap__handle_ingress(void* aContext, otMessage* aMessage, const otMessageInfo* aMessageInfo)
{
    dterr_t* dterr = NULL;
    dtnetportal_coap_t* self = (dtnetportal_coap_t*)aContext;
    dtbuffer_t* buffer = NULL;

    char topic[128];
    otCoapOptionIterator it;
    const otCoapOption* opt;
    otCoapOptionIteratorInit(&it, aMessage);
    otCoapCode code = OT_COAP_CODE_EMPTY;

    // const otCoapOption *otCoapOptionIteratorGetNextOption(otCoapOptionIterator *aIterator)

    topic[0] = '\0';
    while ((opt = otCoapOptionIteratorGetNextOption(&it)) != NULL)
    {
        if (opt->mNumber == OT_COAP_OPTION_URI_QUERY)
        {
            // first query segment (separated by '&') is our topic, we ignore the rest
            if (topic[0] == '\0')
            {
                if (opt->mLength >= sizeof(topic))
                {
                    dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "topic too long");
                    code = OT_COAP_CODE_BAD_REQUEST;
                    goto cleanup;
                }

                otCoapOptionIteratorGetOptionValue(&it, topic);
                topic[opt->mLength] = '\0';
            }
        }
    }

    if (topic[0] == '\0')
    {
        dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "no topic");
        code = OT_COAP_CODE_BAD_REQUEST;
        goto cleanup;
    }

    uint16_t start = otMessageGetOffset(aMessage);     // parser has left the offset positioned after headers
    uint16_t length = otMessageGetLength(aMessage);    // end of entire message buffer
    length = (length >= start) ? (length - start) : 0; // length of payload, if any
    DTERR_C(dtbuffer_create(&buffer, length));         // space for the payload
    if (length)
    {
        // read possibly chunked payload into buffer
        int bytes_read = otMessageRead(aMessage, start, buffer->payload, length);
        if (bytes_read != length)
        {
            dterr = dterr_new(DTERR_FAIL, DTERR_LOC, NULL, "short read %d/%d", bytes_read, length);
            code = OT_COAP_CODE_INTERNAL_ERROR;
            goto cleanup;
        }
    }

    DTERR_C(dtmanifold_publish(self->manifold, topic, buffer));

cleanup:
    // release copy of payload
    dtbuffer_dispose(buffer);

    // send piggyback response if needed
    if (otCoapMessageGetType(aMessage) == OT_COAP_TYPE_CONFIRMABLE)
    {
        otMessage* ack = otCoapNewMessage(self->ot_instance, NULL);
        if (ack)
        {
            // code hasn't been set to anything yet?
            if (code == OT_COAP_CODE_EMPTY)
            {
                if (dterr == NULL)
                    code = OT_COAP_CODE_CHANGED; // success
                else
                    code = OT_COAP_CODE_INTERNAL_ERROR;
            }

            otCoapMessageInitResponse(ack, aMessage, OT_COAP_TYPE_ACKNOWLEDGMENT, code);
            (void)otCoapSendResponse(self->ot_instance, ack, aMessageInfo);
        }
    }

    if (dterr != NULL)
    {
        if (self->config.handle_error_cb != NULL)
        {
            dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "failed to consume incoming coap message");

            // give error to handler, who is expected to take ownership of it
            self->config.handle_error_cb((void*)self, dterr);
        }
        else
        {
            // no handler, so just silently dispose of the error
            dterr_dispose(dterr);
        }
    }
}

// --------------------------------------------------------------------------------------
void
dtnetportal_coap__handle_response_to_publish(void* aContext, otMessage* aMessage, const otMessageInfo* aInfo, otError aResult)
{
    ARG_UNUSED(aInfo);

    if (aResult != OT_ERROR_NONE || !aMessage)
    {
        dtlog_debug(TAG, "CoAP NON request failed: %d (%s)", aResult, otThreadErrorToString(aResult));
    }
    else
    {
#ifndef dtlog_debug
        // Grab the response code
        uint8_t code = otCoapMessageGetCode(aMessage);
        uint8_t codeClass = code >> 5;    // upper 3 bits
        uint8_t codeDetail = code & 0x1F; // lower 5 bits

        dtlog_debug(TAG, "got CoAP response code: %u.%02u", codeClass, codeDetail);
#endif
    }
}

// --------------------------------------------------------------------------------------
// SRP state callback

void
dtnetportal_coap__srp_state_cb(otError error,
  const otSrpClientHostInfo* host,
  const otSrpClientService* services,
  const otSrpClientService* removed,
  void* ctx)
{
    ARG_UNUSED(ctx);

    if (error == OT_ERROR_NONE)
    {
        dtlog_info(TAG,
          "%s(): SRP ok host=%s state=%d",
          __func__,
          host && host->mName ? host->mName : "(null)",
          host ? host->mState : -1);
    }
    // this happens we try to register the same service again on the same ipadress
    // can cause a new name by modifying DTNETPORTAL_COAP_VERSION_MAJOR/MINOR which is part of the hostname
    else if (error == OT_ERROR_DUPLICATED)
    {
        dtlog_debug(TAG, "%s(): host \"%s\" SRP already registered", __func__, host && host->mName ? host->mName : "(null)");
    }
    else
    {
        dtlog_info(TAG,
          "%s(): host \"%s\" SRP error %d (%s)",
          __func__,
          host && host->mName ? host->mName : "(null)",
          error,
          otThreadErrorToString(error));
    }
    /* Note: `removed` is a linked list of services that were removed. */
}