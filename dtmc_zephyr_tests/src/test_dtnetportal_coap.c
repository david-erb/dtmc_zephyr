#include <stdio.h>
#include <string.h>

#include <dtcore/dtbuffer.h>
#include <dtcore/dterr.h>
#include <dtcore/dtlog.h>
#include <dtcore/dtunittest.h>
#include <dtmc_base/dtbufferqueue.h>
#include <dtmc_base/dttasker.h>

#include <dtmc_base/dtnetportal.h>
#include <dtmc_base/dtruntime.h>

#include <dtmc/dtmc_zephyr.h>
#include <dtmc/dtnetportal_coap.h>

#define TAG "test_dtmc_zephyr_dtnetportal_coap"

typedef struct simple_receiver_t
{
    dtbufferqueue_handle bufferqueue_handle;
} simple_receiver_t;

static dterr_t* test_dtmc_zephyr_dtnetportal_coap_dterrs = NULL;

// called when any callback has seen an error
void
dtnetportal_coap_handle_error(void* context, dterr_t* dterr)
{
    if (test_dtmc_zephyr_dtnetportal_coap_dterrs == NULL)
    {
        test_dtmc_zephyr_dtnetportal_coap_dterrs = dterr;
    }
    else
    {
        dterr_append(test_dtmc_zephyr_dtnetportal_coap_dterrs, dterr);
    }
}

// --------------------------------------------------------------------------------------------
// this is called by the manifold publish, which was called in the coap receiver callback
static dterr_t*
test_dtmc_zephyr_dtnetportal_coap_topic1_receive_callback(void* receiver_self, const char* topic, dtbuffer_t* buffer)
{
    dterr_t* dterr = NULL;
    simple_receiver_t* receiver = (simple_receiver_t*)receiver_self;

    // important to do a NOWAIT here or else give control to the consumer after it pushes the buffer even if it doesn't block
    DTERR_C(dtbufferqueue_put(receiver->bufferqueue_handle, buffer, DTTIMEOUT_NOWAIT, NULL));

cleanup:
    if (dterr != NULL)
    {
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "failed to put receive buffer into queue");
    }

    return dterr;
}

/* --- public API ------------------------------------------------------ */

static dterr_t*
test_dtmc_zephyr_dtnetportal_coap_subscribe_twice(void)
{
    dterr_t* dterr = NULL;
    dtbufferqueue_handle bufferqueue1_handle = NULL;
    dtbufferqueue_handle bufferqueue2_handle = NULL;
    dtnetportal_handle netportal_handle = NULL;
    dtbuffer_t* send_buffer = NULL;
    dtbuffer_t* receiver_buffer = NULL;

    // make a bufferqueue for the first receiver
    DTERR_C(dtbufferqueue_create(&bufferqueue1_handle, 10, false));

    // make a bufferqueue for the second receiver
    DTERR_C(dtbufferqueue_create(&bufferqueue2_handle, 10, false));

    // make a netportal for sending/receiving
    {
        dtnetportal_coap_t* object = NULL;
        DTERR_C(dtnetportal_coap_create(&object));
        netportal_handle = (dtnetportal_handle)object;
        dtnetportal_coap_config_t config = { 0 };

        config.dataset_tlvs_hex =
          "000300001a4a0300000b35060004001fffe00208c629b7fc3a73cf050708fd5927e468cb58e9051003185f7475b3f810d950ac47b3df4fdb030f"
          "4f70656e5468726561642d3861616601028aaf04105651d861e8e97bb773eb86cc577a92db0c0402a0f7f80e080000000000010000";

        // connect to local CoAP server (will subsititute mleid address later)
        config.publish_to_host = "::1";
        config.publish_to_port = 5683;
        config.handle_error_cb = dtnetportal_coap_handle_error;
        DTERR_C(dtnetportal_coap_configure(object, &config));
    }

    // make receivers objects (simple objects in this test)
    simple_receiver_t receiver1 = { .bufferqueue_handle = bufferqueue1_handle };
    simple_receiver_t receiver2 = { .bufferqueue_handle = bufferqueue2_handle };

    // Connect
    DTERR_C(dtnetportal_activate(netportal_handle));

    {
        dtnetportal_info_t info;
        DTERR_C(dtnetportal_get_info(netportal_handle, &info));
        dtlog_info(TAG, "netportal listening at %s", info.listening_origin);
    }

    // Subscribe
    const char* topic1 = "test";
    const char* data1 = "Hello, Topic 1!";

    DTERR_C(
      dtnetportal_subscribe(netportal_handle, topic1, &receiver1, test_dtmc_zephyr_dtnetportal_coap_topic1_receive_callback));
    DTERR_C(
      dtnetportal_subscribe(netportal_handle, topic1, &receiver2, test_dtmc_zephyr_dtnetportal_coap_topic1_receive_callback));

    // Send
    DTERR_C(dtbuffer_create(&send_buffer, 32));
    strcpy(send_buffer->payload, data1);
    DTERR_C(dtnetportal_publish(netportal_handle, topic1, send_buffer));
    dtbuffer_dispose(send_buffer);
    send_buffer = NULL;

    // wait for first receiver to get its data
    DTERR_C(dtbufferqueue_get(bufferqueue1_handle, &receiver_buffer, 3000, NULL));

    DTUNITTEST_ASSERT_NOT_NULL(receiver_buffer);
    DTUNITTEST_ASSERT_EQUAL_STRING((char*)receiver_buffer->payload, data1);
    dtbuffer_dispose(receiver_buffer);
    receiver_buffer = NULL;

    // wait for second receiver to get its data
    DTERR_C(dtbufferqueue_get(bufferqueue2_handle, &receiver_buffer, 3000, NULL));
    DTUNITTEST_ASSERT_NOT_NULL(receiver_buffer);
    DTUNITTEST_ASSERT_EQUAL_STRING((char*)receiver_buffer->payload, data1);
    dtbuffer_dispose(receiver_buffer);
    receiver_buffer = NULL;

cleanup:
    if (test_dtmc_zephyr_dtnetportal_coap_dterrs != NULL)
    {
        if (dterr == NULL)
        {
            dterr = test_dtmc_zephyr_dtnetportal_coap_dterrs;
        }
        else
        {
            dterr_append(test_dtmc_zephyr_dtnetportal_coap_dterrs, dterr);
            dterr = test_dtmc_zephyr_dtnetportal_coap_dterrs;
        }
    }

    dtbuffer_dispose(receiver_buffer);
    dtbuffer_dispose(send_buffer);

    //  wait a bit for coap tasks to finish up
    dtruntime_sleep_milliseconds(1000);

    dtnetportal_dispose(netportal_handle);

    dtbufferqueue_dispose(bufferqueue2_handle);
    dtbufferqueue_dispose(bufferqueue1_handle);

    return dterr;
}

// --------------------------------------------------------------------------------------------
void
test_dtmc_zephyr_dtnetportal_coap(DTUNITTEST_SUITE_ARGS)
{
    DTUNITTEST_RUN_TEST(test_dtmc_zephyr_dtnetportal_coap_subscribe_twice);
}
