/*
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * WARNING: Auto-generated code.
 *
 * To make changes to this file, change
 * tools/codegen/core/gen_static_metadata.py, and then re-run it.
 *
 * See metadata.h for an explanation of the interface here, and metadata.cc for
 * an explanation of what's going on.
 */

#ifndef GRPC_CORE_LIB_SLICE_STATIC_SLICE_H
#define GRPC_CORE_LIB_SLICE_STATIC_SLICE_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <type_traits>

#include "src/core/lib/slice/slice_refcount_base.h"
#include "src/core/lib/slice/slice_utils.h"

static_assert(
    std::is_trivially_destructible<grpc_core::StaticMetadataSlice>::value,
    "StaticMetadataSlice must be trivially destructible.");
#define GRPC_STATIC_MDSTR_COUNT 105
/* ":path" */
#define GRPC_MDSTR_PATH (::grpc_core::g_static_metadata_slice_table[0])
/* ":method" */
#define GRPC_MDSTR_METHOD (::grpc_core::g_static_metadata_slice_table[1])
/* ":status" */
#define GRPC_MDSTR_STATUS (::grpc_core::g_static_metadata_slice_table[2])
/* ":authority" */
#define GRPC_MDSTR_AUTHORITY (::grpc_core::g_static_metadata_slice_table[3])
/* ":scheme" */
#define GRPC_MDSTR_SCHEME (::grpc_core::g_static_metadata_slice_table[4])
/* "grpc-message" */
#define GRPC_MDSTR_GRPC_MESSAGE (::grpc_core::g_static_metadata_slice_table[5])
/* "grpc-status" */
#define GRPC_MDSTR_GRPC_STATUS (::grpc_core::g_static_metadata_slice_table[6])
/* "grpc-payload-bin" */
#define GRPC_MDSTR_GRPC_PAYLOAD_BIN \
  (::grpc_core::g_static_metadata_slice_table[7])
/* "grpc-encoding" */
#define GRPC_MDSTR_GRPC_ENCODING (::grpc_core::g_static_metadata_slice_table[8])
/* "grpc-accept-encoding" */
#define GRPC_MDSTR_GRPC_ACCEPT_ENCODING \
  (::grpc_core::g_static_metadata_slice_table[9])
/* "grpc-server-stats-bin" */
#define GRPC_MDSTR_GRPC_SERVER_STATS_BIN \
  (::grpc_core::g_static_metadata_slice_table[10])
/* "grpc-tags-bin" */
#define GRPC_MDSTR_GRPC_TAGS_BIN \
  (::grpc_core::g_static_metadata_slice_table[11])
/* "grpc-trace-bin" */
#define GRPC_MDSTR_GRPC_TRACE_BIN \
  (::grpc_core::g_static_metadata_slice_table[12])
/* "content-type" */
#define GRPC_MDSTR_CONTENT_TYPE (::grpc_core::g_static_metadata_slice_table[13])
/* "content-encoding" */
#define GRPC_MDSTR_CONTENT_ENCODING \
  (::grpc_core::g_static_metadata_slice_table[14])
/* "accept-encoding" */
#define GRPC_MDSTR_ACCEPT_ENCODING \
  (::grpc_core::g_static_metadata_slice_table[15])
/* "grpc-internal-encoding-request" */
#define GRPC_MDSTR_GRPC_INTERNAL_ENCODING_REQUEST \
  (::grpc_core::g_static_metadata_slice_table[16])
/* "host" */
#define GRPC_MDSTR_HOST (::grpc_core::g_static_metadata_slice_table[17])
/* "grpc-previous-rpc-attempts" */
#define GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS \
  (::grpc_core::g_static_metadata_slice_table[18])
/* "grpc-retry-pushback-ms" */
#define GRPC_MDSTR_GRPC_RETRY_PUSHBACK_MS \
  (::grpc_core::g_static_metadata_slice_table[19])
/* "x-endpoint-load-metrics-bin" */
#define GRPC_MDSTR_X_ENDPOINT_LOAD_METRICS_BIN \
  (::grpc_core::g_static_metadata_slice_table[20])
/* "grpc-timeout" */
#define GRPC_MDSTR_GRPC_TIMEOUT (::grpc_core::g_static_metadata_slice_table[21])
/* "user-agent" */
#define GRPC_MDSTR_USER_AGENT (::grpc_core::g_static_metadata_slice_table[22])
/* "1" */
#define GRPC_MDSTR_1 (::grpc_core::g_static_metadata_slice_table[23])
/* "2" */
#define GRPC_MDSTR_2 (::grpc_core::g_static_metadata_slice_table[24])
/* "3" */
#define GRPC_MDSTR_3 (::grpc_core::g_static_metadata_slice_table[25])
/* "4" */
#define GRPC_MDSTR_4 (::grpc_core::g_static_metadata_slice_table[26])
/* "" */
#define GRPC_MDSTR_EMPTY (::grpc_core::g_static_metadata_slice_table[27])
/* "/grpc.lb.v1.LoadBalancer/BalanceLoad" */
#define GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD \
  (::grpc_core::g_static_metadata_slice_table[28])
/* "/envoy.service.load_stats.v2.LoadReportingService/StreamLoadStats" */
#define GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_LOAD_STATS_DOT_V2_DOT_LOADREPORTINGSERVICE_SLASH_STREAMLOADSTATS \
  (::grpc_core::g_static_metadata_slice_table[29])
/* "/envoy.service.load_stats.v3.LoadReportingService/StreamLoadStats" */
#define GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_LOAD_STATS_DOT_V3_DOT_LOADREPORTINGSERVICE_SLASH_STREAMLOADSTATS \
  (::grpc_core::g_static_metadata_slice_table[30])
/* "/grpc.health.v1.Health/Watch" */
#define GRPC_MDSTR_SLASH_GRPC_DOT_HEALTH_DOT_V1_DOT_HEALTH_SLASH_WATCH \
  (::grpc_core::g_static_metadata_slice_table[31])
/* "/envoy.service.discovery.v2.AggregatedDiscoveryService/StreamAggregatedResources"
 */
#define GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_DISCOVERY_DOT_V2_DOT_AGGREGATEDDISCOVERYSERVICE_SLASH_STREAMAGGREGATEDRESOURCES \
  (::grpc_core::g_static_metadata_slice_table[32])
/* "/envoy.service.discovery.v3.AggregatedDiscoveryService/StreamAggregatedResources"
 */
#define GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_DISCOVERY_DOT_V3_DOT_AGGREGATEDDISCOVERYSERVICE_SLASH_STREAMAGGREGATEDRESOURCES \
  (::grpc_core::g_static_metadata_slice_table[33])
/* "deflate" */
#define GRPC_MDSTR_DEFLATE (::grpc_core::g_static_metadata_slice_table[34])
/* "gzip" */
#define GRPC_MDSTR_GZIP (::grpc_core::g_static_metadata_slice_table[35])
/* "stream/gzip" */
#define GRPC_MDSTR_STREAM_SLASH_GZIP \
  (::grpc_core::g_static_metadata_slice_table[36])
/* "te" */
#define GRPC_MDSTR_TE (::grpc_core::g_static_metadata_slice_table[37])
/* "trailers" */
#define GRPC_MDSTR_TRAILERS (::grpc_core::g_static_metadata_slice_table[38])
/* "GET" */
#define GRPC_MDSTR_GET (::grpc_core::g_static_metadata_slice_table[39])
/* "POST" */
#define GRPC_MDSTR_POST (::grpc_core::g_static_metadata_slice_table[40])
/* "/" */
#define GRPC_MDSTR_SLASH (::grpc_core::g_static_metadata_slice_table[41])
/* "/index.html" */
#define GRPC_MDSTR_SLASH_INDEX_DOT_HTML \
  (::grpc_core::g_static_metadata_slice_table[42])
/* "http" */
#define GRPC_MDSTR_HTTP (::grpc_core::g_static_metadata_slice_table[43])
/* "https" */
#define GRPC_MDSTR_HTTPS (::grpc_core::g_static_metadata_slice_table[44])
/* "200" */
#define GRPC_MDSTR_200 (::grpc_core::g_static_metadata_slice_table[45])
/* "204" */
#define GRPC_MDSTR_204 (::grpc_core::g_static_metadata_slice_table[46])
/* "206" */
#define GRPC_MDSTR_206 (::grpc_core::g_static_metadata_slice_table[47])
/* "304" */
#define GRPC_MDSTR_304 (::grpc_core::g_static_metadata_slice_table[48])
/* "400" */
#define GRPC_MDSTR_400 (::grpc_core::g_static_metadata_slice_table[49])
/* "404" */
#define GRPC_MDSTR_404 (::grpc_core::g_static_metadata_slice_table[50])
/* "500" */
#define GRPC_MDSTR_500 (::grpc_core::g_static_metadata_slice_table[51])
/* "accept-charset" */
#define GRPC_MDSTR_ACCEPT_CHARSET \
  (::grpc_core::g_static_metadata_slice_table[52])
/* "gzip, deflate" */
#define GRPC_MDSTR_GZIP_COMMA_DEFLATE \
  (::grpc_core::g_static_metadata_slice_table[53])
/* "accept-language" */
#define GRPC_MDSTR_ACCEPT_LANGUAGE \
  (::grpc_core::g_static_metadata_slice_table[54])
/* "accept-ranges" */
#define GRPC_MDSTR_ACCEPT_RANGES \
  (::grpc_core::g_static_metadata_slice_table[55])
/* "accept" */
#define GRPC_MDSTR_ACCEPT (::grpc_core::g_static_metadata_slice_table[56])
/* "access-control-allow-origin" */
#define GRPC_MDSTR_ACCESS_CONTROL_ALLOW_ORIGIN \
  (::grpc_core::g_static_metadata_slice_table[57])
/* "age" */
#define GRPC_MDSTR_AGE (::grpc_core::g_static_metadata_slice_table[58])
/* "allow" */
#define GRPC_MDSTR_ALLOW (::grpc_core::g_static_metadata_slice_table[59])
/* "authorization" */
#define GRPC_MDSTR_AUTHORIZATION \
  (::grpc_core::g_static_metadata_slice_table[60])
/* "cache-control" */
#define GRPC_MDSTR_CACHE_CONTROL \
  (::grpc_core::g_static_metadata_slice_table[61])
/* "content-disposition" */
#define GRPC_MDSTR_CONTENT_DISPOSITION \
  (::grpc_core::g_static_metadata_slice_table[62])
/* "content-language" */
#define GRPC_MDSTR_CONTENT_LANGUAGE \
  (::grpc_core::g_static_metadata_slice_table[63])
/* "content-length" */
#define GRPC_MDSTR_CONTENT_LENGTH \
  (::grpc_core::g_static_metadata_slice_table[64])
/* "content-location" */
#define GRPC_MDSTR_CONTENT_LOCATION \
  (::grpc_core::g_static_metadata_slice_table[65])
/* "content-range" */
#define GRPC_MDSTR_CONTENT_RANGE \
  (::grpc_core::g_static_metadata_slice_table[66])
/* "cookie" */
#define GRPC_MDSTR_COOKIE (::grpc_core::g_static_metadata_slice_table[67])
/* "date" */
#define GRPC_MDSTR_DATE (::grpc_core::g_static_metadata_slice_table[68])
/* "etag" */
#define GRPC_MDSTR_ETAG (::grpc_core::g_static_metadata_slice_table[69])
/* "expect" */
#define GRPC_MDSTR_EXPECT (::grpc_core::g_static_metadata_slice_table[70])
/* "expires" */
#define GRPC_MDSTR_EXPIRES (::grpc_core::g_static_metadata_slice_table[71])
/* "from" */
#define GRPC_MDSTR_FROM (::grpc_core::g_static_metadata_slice_table[72])
/* "if-match" */
#define GRPC_MDSTR_IF_MATCH (::grpc_core::g_static_metadata_slice_table[73])
/* "if-modified-since" */
#define GRPC_MDSTR_IF_MODIFIED_SINCE \
  (::grpc_core::g_static_metadata_slice_table[74])
/* "if-none-match" */
#define GRPC_MDSTR_IF_NONE_MATCH \
  (::grpc_core::g_static_metadata_slice_table[75])
/* "if-range" */
#define GRPC_MDSTR_IF_RANGE (::grpc_core::g_static_metadata_slice_table[76])
/* "if-unmodified-since" */
#define GRPC_MDSTR_IF_UNMODIFIED_SINCE \
  (::grpc_core::g_static_metadata_slice_table[77])
/* "last-modified" */
#define GRPC_MDSTR_LAST_MODIFIED \
  (::grpc_core::g_static_metadata_slice_table[78])
/* "link" */
#define GRPC_MDSTR_LINK (::grpc_core::g_static_metadata_slice_table[79])
/* "location" */
#define GRPC_MDSTR_LOCATION (::grpc_core::g_static_metadata_slice_table[80])
/* "max-forwards" */
#define GRPC_MDSTR_MAX_FORWARDS (::grpc_core::g_static_metadata_slice_table[81])
/* "proxy-authenticate" */
#define GRPC_MDSTR_PROXY_AUTHENTICATE \
  (::grpc_core::g_static_metadata_slice_table[82])
/* "proxy-authorization" */
#define GRPC_MDSTR_PROXY_AUTHORIZATION \
  (::grpc_core::g_static_metadata_slice_table[83])
/* "range" */
#define GRPC_MDSTR_RANGE (::grpc_core::g_static_metadata_slice_table[84])
/* "referer" */
#define GRPC_MDSTR_REFERER (::grpc_core::g_static_metadata_slice_table[85])
/* "refresh" */
#define GRPC_MDSTR_REFRESH (::grpc_core::g_static_metadata_slice_table[86])
/* "retry-after" */
#define GRPC_MDSTR_RETRY_AFTER (::grpc_core::g_static_metadata_slice_table[87])
/* "server" */
#define GRPC_MDSTR_SERVER (::grpc_core::g_static_metadata_slice_table[88])
/* "set-cookie" */
#define GRPC_MDSTR_SET_COOKIE (::grpc_core::g_static_metadata_slice_table[89])
/* "strict-transport-security" */
#define GRPC_MDSTR_STRICT_TRANSPORT_SECURITY \
  (::grpc_core::g_static_metadata_slice_table[90])
/* "transfer-encoding" */
#define GRPC_MDSTR_TRANSFER_ENCODING \
  (::grpc_core::g_static_metadata_slice_table[91])
/* "vary" */
#define GRPC_MDSTR_VARY (::grpc_core::g_static_metadata_slice_table[92])
/* "via" */
#define GRPC_MDSTR_VIA (::grpc_core::g_static_metadata_slice_table[93])
/* "www-authenticate" */
#define GRPC_MDSTR_WWW_AUTHENTICATE \
  (::grpc_core::g_static_metadata_slice_table[94])
/* "0" */
#define GRPC_MDSTR_0 (::grpc_core::g_static_metadata_slice_table[95])
/* "identity" */
#define GRPC_MDSTR_IDENTITY (::grpc_core::g_static_metadata_slice_table[96])
/* "application/grpc" */
#define GRPC_MDSTR_APPLICATION_SLASH_GRPC \
  (::grpc_core::g_static_metadata_slice_table[97])
/* "grpc" */
#define GRPC_MDSTR_GRPC (::grpc_core::g_static_metadata_slice_table[98])
/* "PUT" */
#define GRPC_MDSTR_PUT (::grpc_core::g_static_metadata_slice_table[99])
/* "lb-cost-bin" */
#define GRPC_MDSTR_LB_COST_BIN (::grpc_core::g_static_metadata_slice_table[100])
/* "identity,deflate" */
#define GRPC_MDSTR_IDENTITY_COMMA_DEFLATE \
  (::grpc_core::g_static_metadata_slice_table[101])
/* "identity,gzip" */
#define GRPC_MDSTR_IDENTITY_COMMA_GZIP \
  (::grpc_core::g_static_metadata_slice_table[102])
/* "deflate,gzip" */
#define GRPC_MDSTR_DEFLATE_COMMA_GZIP \
  (::grpc_core::g_static_metadata_slice_table[103])
/* "identity,deflate,gzip" */
#define GRPC_MDSTR_IDENTITY_COMMA_DEFLATE_COMMA_GZIP \
  (::grpc_core::g_static_metadata_slice_table[104])

namespace grpc_core {
extern StaticSliceRefcount
    g_static_metadata_slice_refcounts[GRPC_STATIC_MDSTR_COUNT];
extern const StaticMetadataSlice
    g_static_metadata_slice_table[GRPC_STATIC_MDSTR_COUNT];
extern const uint8_t g_static_metadata_bytes[];
}  // namespace grpc_core

#define GRPC_IS_STATIC_METADATA_STRING(slice) \
  ((slice).refcount != NULL &&                \
   (slice).refcount->GetType() == grpc_slice_refcount::Type::STATIC)

#define GRPC_STATIC_METADATA_INDEX(static_slice)        \
  (reinterpret_cast<::grpc_core::StaticSliceRefcount*>( \
       (static_slice).refcount)                         \
       ->index)

#endif /* GRPC_CORE_LIB_SLICE_STATIC_SLICE_H */
