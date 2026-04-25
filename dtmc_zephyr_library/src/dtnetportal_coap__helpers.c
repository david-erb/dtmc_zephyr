// POSIX dtnetportal over MQTT using libcoap, with dtmanifold fanout.

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>

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

#include "dtnetportal_coap_private.h"

#define TAG "dtnetportal_coap__helpers"

// #define dtlog_debug(TAG, ...)

// --------------------------------------------------------------------------------------
static int
_hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

// --------------------------------------------------------------------------------------
static int
_parse_hex(const char* hex, uint8_t* out, size_t out_cap, size_t* out_len)
{
    // Accepts with/without "0x", ignores whitespace, accepts even number of hex digits.
    size_t w = 0;
    int high = -1;

    for (const char* p = hex; *p; ++p)
    {
        char c = *p;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ':')
            continue;
        if (c == 'x' || c == 'X') // allow "...0x..."
            continue;

        int n = _hex_nibble(c);
        if (n < 0)
            return -EINVAL;

        if (high < 0)
        {
            high = n;
        }
        else
        {
            if (w >= out_cap)
                return -ENOSPC;
            out[w++] = (uint8_t)((high << 4) | n);
            high = -1;
        }
    }
    if (high >= 0)
        return -EINVAL; // odd number of digits
    *out_len = w;
    return 0;
}

// --------------------------------------------------------------------------------------
// start Thread from an Active Operational Dataset (TLVs in hex).
dterr_t*
dtnetportal_coap__start_thread(dtnetportal_coap_t* self, uint32_t attach_timeout_ms)
{
    dterr_t* dterr = NULL;

    if (!self->config.dataset_tlvs_hex || !*self->config.dataset_tlvs_hex)
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "called with NULL or empty dataset_tlvs_hex");
        goto cleanup;
    }

    self->ot_instance = openthread_get_default_instance();
    if (!self->ot_instance)
    {
        dterr = dterr_new(DTERR_INFRASTRUCTURE, DTERR_LOC, NULL, "openThread instance is NULL");
        goto cleanup;
    }

    // Ensure we never reuse persisted state (matches your "no persistence" requirement).
    // Safe if nothing is stored; forces a clean slate each boot.
    otError oe = otInstanceErasePersistentInfo(self->ot_instance);
    if (oe != OT_ERROR_NONE)
    {
        dterr = dterr_new(
          DTERR_INFRASTRUCTURE, DTERR_LOC, NULL, "erase persistent info failed %d (%s)", oe, dtnetportal_coap__err_str(oe));
        goto cleanup;
    }

    // Parse TLVs into buffer
    uint8_t tlv_buf[256];
    size_t tlv_len = 0;
    int pr = _parse_hex(self->config.dataset_tlvs_hex, tlv_buf, sizeof(tlv_buf), &tlv_len);
    if (pr != 0)
    {
        dterr = dterr_new(
          DTERR_BADARG, DTERR_LOC, NULL, "dataset TLVs hex parse error (%d). Check for non-hex or odd digit count", pr);
        goto cleanup;
    }

    //
    otOperationalDatasetTlvs tlvs = { 0 };
    if (tlv_len > sizeof(tlvs.mTlvs))
    {
        dterr = dterr_new(DTERR_BADARG, DTERR_LOC, NULL, "dataset too large: %zu bytes", tlv_len);
        goto cleanup;
    }
    memcpy(tlvs.mTlvs, tlv_buf, tlv_len);
    tlvs.mLength = (uint8_t)tlv_len;

    oe = otDatasetSetActiveTlvs(self->ot_instance, &tlvs);

    if (oe != OT_ERROR_NONE)
    {
        dterr = dterr_new(
          DTERR_INFRASTRUCTURE, DTERR_LOC, NULL, "set active dataset TLVs failed %d (%s)", oe, dtnetportal_coap__err_str(oe));
        goto cleanup;
    }

    // Bring up IPv6 + Thread
    oe = otIp6SetEnabled(self->ot_instance, true);
    if (oe != OT_ERROR_NONE)
    {
        dterr =
          dterr_new(DTERR_INFRASTRUCTURE, DTERR_LOC, NULL, "enable IPv6 failed %d (%s)", oe, dtnetportal_coap__err_str(oe));
        goto cleanup;
    }

    oe = otThreadSetEnabled(self->ot_instance, true);
    if (oe != OT_ERROR_NONE)
    {
        dterr =
          dterr_new(DTERR_INFRASTRUCTURE, DTERR_LOC, NULL, "enable Thread failed %d (%s)", oe, dtnetportal_coap__err_str(oe));
        goto cleanup;
    }

    // Wait for attach
    uint64_t deadline = k_uptime_get() + attach_timeout_ms;
    bool got_role = false;
    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;
    while ((int64_t)(deadline - k_uptime_get()) > 0)
    {
        role = otThreadGetDeviceRole(self->ot_instance);
        if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER)
        {
            dtlog_info(TAG, "thread attached as role %s", dtnetportal_coap__role_str(role));
            got_role = true;
            break;
        }
        k_sleep(K_MSEC(100));
    }

    // Timed out
    if (!got_role)
    {
        dterr = dterr_new(DTERR_TIMEOUT,
          DTERR_LOC,
          NULL,
          "thread attach timeout after %u ms with last role %s",
          attach_timeout_ms,
          dtnetportal_coap__role_str(role));
        goto cleanup;
    }

cleanup:
    if (dterr != NULL)
        dterr = dterr_new(dterr->error_code, DTERR_LOC, dterr, "unable to start Thread");

    return dterr;
}

// --------------------------------------------------------------------------------------
const char*
dtnetportal_coap__err_str(otError err)
{
    switch (err)
    {
        case OT_ERROR_NONE:
            return "NONE";
        case OT_ERROR_FAILED:
            return "FAILED";
        case OT_ERROR_DROP:
            return "DROP";
        case OT_ERROR_NO_BUFS:
            return "NO_BUFS";
        case OT_ERROR_NO_ROUTE:
            return "NO_ROUTE";
        case OT_ERROR_BUSY:
            return "BUSY";
        case OT_ERROR_PARSE:
            return "PARSE";
        case OT_ERROR_INVALID_ARGS:
            return "INVALID_ARGS";
        case OT_ERROR_SECURITY:
            return "SECURITY";
        case OT_ERROR_ADDRESS_QUERY:
            return "ADDRESS_QUERY";
        case OT_ERROR_NO_ADDRESS:
            return "NO_ADDRESS";
        case OT_ERROR_ABORT:
            return "ABORT";
        case OT_ERROR_NOT_IMPLEMENTED:
            return "NOT_IMPLEMENTED";
        case OT_ERROR_INVALID_STATE:
            return "INVALID_STATE";
        case OT_ERROR_NO_ACK:
            return "NO_ACK";
        case OT_ERROR_CHANNEL_ACCESS_FAILURE:
            return "CHANNEL_ACCESS_FAILURE";
        case OT_ERROR_DETACHED:
            return "DETACHED";
        case OT_ERROR_FCS:
            return "FCS";
        case OT_ERROR_NO_FRAME_RECEIVED:
            return "NO_FRAME_RECEIVED";
        case OT_ERROR_UNKNOWN_NEIGHBOR:
            return "UNKNOWN_NEIGHBOR";
        case OT_ERROR_INVALID_SOURCE_ADDRESS:
            return "INVALID_SOURCE_ADDRESS";
        case OT_ERROR_ADDRESS_FILTERED:
            return "ADDRESS_FILTERED";
        case OT_ERROR_DESTINATION_ADDRESS_FILTERED:
            return "DESTINATION_ADDRESS_FILTERED";
        case OT_ERROR_NOT_FOUND:
            return "NOT_FOUND";
        case OT_ERROR_ALREADY:
            return "ALREADY";
        case OT_ERROR_IP6_ADDRESS_CREATION_FAILURE:
            return "IP6_ADDRESS_CREATION_FAILURE";
        case OT_ERROR_NOT_CAPABLE:
            return "NOT_CAPABLE";
        case OT_ERROR_RESPONSE_TIMEOUT:
            return "RESPONSE_TIMEOUT";
        case OT_ERROR_DUPLICATED:
            return "DUPLICATED";
        case OT_ERROR_REASSEMBLY_TIMEOUT:
            return "REASSEMBLY_TIMEOUT";
        case OT_ERROR_NOT_TMF:
            return "NOT_TMF";
        case OT_ERROR_NOT_LOWPAN_DATA_FRAME:
            return "NOT_LOWPAN_DATA_FRAME";
        case OT_ERROR_LINK_MARGIN_LOW:
            return "LINK_MARGIN_LOW";
        case OT_ERROR_INVALID_COMMAND:
            return "INVALID_COMMAND";
        case OT_ERROR_PENDING:
            return "PENDING";
        case OT_ERROR_REJECTED:
            return "REJECTED";
        case OT_ERROR_GENERIC:
            return "GENERIC";
        default:
            return "UNKNOWN";
    }
}

// --------------------------------------------------------------------------------------
const char*
dtnetportal_coap__role_str(otDeviceRole role)
{
    switch (role)
    {
        case OT_DEVICE_ROLE_DISABLED:
            return "DISABLED";
        case OT_DEVICE_ROLE_DETACHED:
            return "DETACHED";
        case OT_DEVICE_ROLE_CHILD:
            return "CHILD";
        case OT_DEVICE_ROLE_ROUTER:
            return "ROUTER";
        case OT_DEVICE_ROLE_LEADER:
            return "LEADER";
        default:
            return "UNKNOWN";
    }
}

// --------------------------------------------------------------------------------------

static bool
_addr_is_link_local(const otIp6Address* a)
{
    return (a->mFields.m8[0] == 0xfe) && ((a->mFields.m8[1] & 0xc0) == 0x80); // fe80::/10
}

// --------------------------------------------------------------------------------------

static bool
_addr_has_prefix(const otIp6Address* a, const uint8_t* prefix_m8, size_t prefix_len_bytes)
{
    return memcmp(a->mFields.m8, prefix_m8, prefix_len_bytes) == 0;
}

// --------------------------------------------------------------------------------------

static const char*
_origin_to_str(uint8_t o)
{
    switch (o)
    {
        case OT_ADDRESS_ORIGIN_THREAD:
            return "origin=THREAD";
        case OT_ADDRESS_ORIGIN_SLAAC:
            return "origin=SLAAC";
        case OT_ADDRESS_ORIGIN_DHCPV6:
            return "origin=DHCPv6";
        case OT_ADDRESS_ORIGIN_MANUAL:
            return "origin=MANUAL";
#if OPENTHREAD_CONFIG_BACKBONE_ROUTER_ENABLE
        case OT_ADDRESS_ORIGIN_BACKBONE:
            return "origin=BACKBONE";
#endif
        default:
            return "origin=UNKNOWN";
    }
}

// --------------------------------------------------------------------------------------

static bool
_addr_is_mlp_rloc(const otIp6Address* a, const otMeshLocalPrefix* mlp, uint16_t* out_rloc16)
{
    if (!mlp)
        return false;

    // Must match Mesh-Local Prefix (/64)
    if (memcmp(a->mFields.m8, mlp->m8, 8) != 0)
        return false;

    // Check IID pattern: 00:00:00:ff:fe:00:XX:XX
    const uint8_t* iid = &a->mFields.m8[8];
    if (iid[0] != 0x00 || iid[1] != 0x00 || iid[2] != 0x00 || iid[3] != 0xff || iid[4] != 0xfe || iid[5] != 0x00)
    {
        return false;
    }

    if (out_rloc16)
    {
        *out_rloc16 = ((uint16_t)iid[6] << 8) | iid[7];
    }
    return true;
}

// --------------------------------------------------------------------------------------

static void
_rloc16_to_str(uint16_t rloc16, char* buf, size_t n)
{
    uint16_t routerId = (uint16_t)(rloc16 >> 10);
    uint16_t childId = (uint16_t)(rloc16 & 0x03ff);
    // childId == 0 usually implies Router/REED; non-zero ⇒ Child
    snprintf(buf, n, "rloc16=0x%04x (routerId=%u childId=%u)", rloc16, routerId, childId);
}

// --------------------------------------------------------------------------------------

void
dtnetportal_coap__each_ipaddr(dtnetportal_coap_ipaddr_cb callback_func, void* callback_context)
{
    if (!callback_func)
        return;

    otInstance* inst = openthread_get_default_instance();
    if (!inst)
    {
        callback_func(callback_context, "(no-instance)", "error=no otInstance");
        return;
    }

    /* Get Mesh-Local EID and Mesh-Local Prefix for classification */
    const otIp6Address* mleid = otThreadGetMeshLocalEid(inst);

    const otMeshLocalPrefix* mlp = NULL;
    mlp = otThreadGetMeshLocalPrefix(inst); // returns 8-byte /64 prefix

    /* Walk unicast addresses */
    const otNetifAddress* ua = otIp6GetUnicastAddresses(inst);
    for (; ua != NULL; ua = ua->mNext)
    {
        char ip[OT_IP6_ADDRESS_STRING_SIZE];
        otIp6AddressToString(&ua->mAddress, ip, sizeof ip);

        /* Classify */
        const char* klass = NULL;
        uint16_t rloc16 = 0;
        bool is_rloc = _addr_is_mlp_rloc(&ua->mAddress, mlp, &rloc16);

        if (_addr_is_link_local(&ua->mAddress))
        {
            klass = "link-local";
        }
        else if (mleid && (memcmp(&ua->mAddress, mleid, sizeof(*mleid)) == 0))
        {
            klass = "mesh-local EID";
        }
        else if (is_rloc)
        {
            klass = "mesh-local (RLOC)";
        }
        else if (_addr_has_prefix(&ua->mAddress, mlp->m8, 8))
        {
            klass = "mesh-local (MLP)";
        }
        else if (ua->mAddress.mFields.m8[0] == 0xfd || ua->mAddress.mFields.m8[0] == 0xfc)
        {
            klass = "ULA (likely OMR/global-on-mesh)";
        }
        else
        {
            klass = "GUA/other";
        }

        /* Build extra info string */
        char extra[160];

        /* mAddressOrigin exists on all current OT versions; guard if headers are older */
        const char* origin = _origin_to_str(ua->mAddressOrigin);
        if (is_rloc)
        {
            char rlocbuf[80];
            _rloc16_to_str(rloc16, rlocbuf, sizeof rlocbuf);
            snprintf(extra, sizeof extra, "%s; %s; %s; preferred=%d", klass, origin, rlocbuf, ua->mPreferred ? 1 : 0);
        }
        else
        {
            snprintf(extra, sizeof extra, "%s; %s; preferred=%d", klass, origin, ua->mPreferred ? 1 : 0);
        }
        callback_func(callback_context, ip, extra);
    }
}

// --------------------------------------------------------------------------------------
/* Make a readable, stable-ish hostname from factory EUI-64 */
void
dtnetportal_coap__make_euid_and_version_based_name(dtnetportal_coap_t* self, const char* prefix, char* out, size_t out_len)
{
    otExtAddress eui;
    otLinkGetFactoryAssignedIeeeEui64(self->ot_instance, &eui);
    snprintk(out,
      out_len,
      "%s__v%02d-%02d__%02x%02x%02x%02x",
      prefix,
      DTNETPORTAL_COAP_VERSION_MAJOR,
      DTNETPORTAL_COAP_VERSION_MINOR,
      eui.m8[4],
      eui.m8[5],
      eui.m8[6],
      eui.m8[7]);
}

// --------------------------------------------------------------------------------------
/* Make a readable, stable-ish hostname from factory EUI-64 */
void
dtnetportal_coap__make_hostname(dtnetportal_coap_t* self, char* out, size_t out_len)
{
    dtnetportal_coap__make_euid_and_version_based_name(self, "dtmc", out, out_len);
}

// --------------------------------------------------------------------------------------
/* Make a readable, stable-ish instance name from factory EUI-64 */
void
dtnetportal_coap__make_instance_name(dtnetportal_coap_t* self, char* out, size_t out_len)
{
    dtnetportal_coap__make_euid_and_version_based_name(self, "dtnetportal", out, out_len);
}

// --------------------------------------------------------------------------------------
dterr_t*
dtnetportal_coap__srp_register(dtnetportal_coap_t* self)
{
    dterr_t* dterr = NULL;

    otInstance* ot = self->ot_instance;

    dtnetportal_coap__make_hostname(self, self->srp_hostname, sizeof(self->srp_hostname));
    dtnetportal_coap__make_instance_name(self, self->srp_instance_name, sizeof(self->srp_instance_name));

    otError oe;
    /* Host: name + let OT pick addresses automatically (ML-EID or preferred GUA) */
    oe = otSrpClientSetHostName(ot, self->srp_hostname);
    if (oe != OT_ERROR_NONE)
    {
        dterr = dterr_new(DTERR_INFRASTRUCTURE,
          DTERR_LOC,
          NULL,
          "set SRP host name \"%s\" failed %d (%s)",
          self->srp_hostname,
          oe,
          dtnetportal_coap__err_str(oe));
        goto cleanup;
    }

    oe = otSrpClientEnableAutoHostAddress(ot);
    if (oe != OT_ERROR_NONE)
    {
        dterr = dterr_new(DTERR_INFRASTRUCTURE,
          DTERR_LOC,
          NULL,
          "enable SRP auto host address failed %d (%s)",
          oe,
          dtnetportal_coap__err_str(oe));
        goto cleanup;
    }

    /* Advertise CoAP service */

    snprintf(self->srp_service_key, sizeof(self->srp_service_key), "dtnetportal_path");
    snprintf(self->srp_service_val, sizeof(self->srp_service_val), "ingress");

    self->srp_dns_text_entry[0].mKey = self->srp_service_key;
    self->srp_dns_text_entry[0].mValue = self->srp_service_val;               // value part only
    self->srp_dns_text_entry[0].mValueLength = strlen(self->srp_service_val); // length of value part only

    memset(&self->srp_service, 0, sizeof(self->srp_service));
    self->srp_service.mName = "_coap._udp";
    self->srp_service.mInstanceName = self->srp_instance_name;
    self->srp_service.mPort = htons(self->config.listen_port);
    self->srp_service.mTxtEntries = self->srp_dns_text_entry;
    self->srp_service.mNumTxtEntries = 1;

    oe = otSrpClientAddService(ot, &self->srp_service);
    if (oe != OT_ERROR_NONE)
    {
        dterr =
          dterr_new(DTERR_INFRASTRUCTURE, DTERR_LOC, NULL, "add SRP service failed %d (%s)", oe, dtnetportal_coap__err_str(oe));
        goto cleanup;
    }

    /* Callback for status */
    otSrpClientSetCallback(ot, dtnetportal_coap__srp_state_cb, self);

    /* Auto-start SRP when the BR’s SRP server is discovered (monitors Network Data) */
    otSrpClientEnableAutoStartMode(ot,
      /*auto-start cb*/ NULL,
      /*ctx*/ self);

    dtlog_info(TAG,
      "requested SRP registration for host \"%s\", service=_coap._udp:%u \"%s\"",
      self->srp_hostname,
      self->config.listen_port,
      self->srp_instance_name);
cleanup:
    return dterr;
}