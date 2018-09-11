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

#ifndef GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H
#define GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/transport/metadata.h"

#define GRPC_STATIC_MDSTR_COUNT 105
extern const grpc_slice grpc_static_slice_table[GRPC_STATIC_MDSTR_COUNT];
/* ":path" */
#define GRPC_MDSTR_PATH (grpc_static_slice_table[0])
/* ":method" */
#define GRPC_MDSTR_METHOD (grpc_static_slice_table[1])
/* ":status" */
#define GRPC_MDSTR_STATUS (grpc_static_slice_table[2])
/* ":authority" */
#define GRPC_MDSTR_AUTHORITY (grpc_static_slice_table[3])
/* ":scheme" */
#define GRPC_MDSTR_SCHEME (grpc_static_slice_table[4])
/* "te" */
#define GRPC_MDSTR_TE (grpc_static_slice_table[5])
/* "grpc-message" */
#define GRPC_MDSTR_GRPC_MESSAGE (grpc_static_slice_table[6])
/* "grpc-status" */
#define GRPC_MDSTR_GRPC_STATUS (grpc_static_slice_table[7])
/* "grpc-payload-bin" */
#define GRPC_MDSTR_GRPC_PAYLOAD_BIN (grpc_static_slice_table[8])
/* "grpc-encoding" */
#define GRPC_MDSTR_GRPC_ENCODING (grpc_static_slice_table[9])
/* "grpc-accept-encoding" */
#define GRPC_MDSTR_GRPC_ACCEPT_ENCODING (grpc_static_slice_table[10])
/* "grpc-server-stats-bin" */
#define GRPC_MDSTR_GRPC_SERVER_STATS_BIN (grpc_static_slice_table[11])
/* "grpc-tags-bin" */
#define GRPC_MDSTR_GRPC_TAGS_BIN (grpc_static_slice_table[12])
/* "grpc-trace-bin" */
#define GRPC_MDSTR_GRPC_TRACE_BIN (grpc_static_slice_table[13])
/* "content-type" */
#define GRPC_MDSTR_CONTENT_TYPE (grpc_static_slice_table[14])
/* "content-encoding" */
#define GRPC_MDSTR_CONTENT_ENCODING (grpc_static_slice_table[15])
/* "accept-encoding" */
#define GRPC_MDSTR_ACCEPT_ENCODING (grpc_static_slice_table[16])
/* "grpc-internal-encoding-request" */
#define GRPC_MDSTR_GRPC_INTERNAL_ENCODING_REQUEST (grpc_static_slice_table[17])
/* "grpc-internal-stream-encoding-request" */
#define GRPC_MDSTR_GRPC_INTERNAL_STREAM_ENCODING_REQUEST (grpc_static_slice_table[18])
/* "user-agent" */
#define GRPC_MDSTR_USER_AGENT (grpc_static_slice_table[19])
/* "host" */
#define GRPC_MDSTR_HOST (grpc_static_slice_table[20])
/* "lb-token" */
#define GRPC_MDSTR_LB_TOKEN (grpc_static_slice_table[21])
/* "grpc-previous-rpc-attempts" */
#define GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS (grpc_static_slice_table[22])
/* "grpc-retry-pushback-ms" */
#define GRPC_MDSTR_GRPC_RETRY_PUSHBACK_MS (grpc_static_slice_table[23])
/* "grpc-timeout" */
#define GRPC_MDSTR_GRPC_TIMEOUT (grpc_static_slice_table[24])
/* "1" */
#define GRPC_MDSTR_1 (grpc_static_slice_table[25])
/* "2" */
#define GRPC_MDSTR_2 (grpc_static_slice_table[26])
/* "3" */
#define GRPC_MDSTR_3 (grpc_static_slice_table[27])
/* "4" */
#define GRPC_MDSTR_4 (grpc_static_slice_table[28])
/* "" */
#define GRPC_MDSTR_EMPTY (grpc_static_slice_table[29])
/* "grpc.wait_for_ready" */
#define GRPC_MDSTR_GRPC_DOT_WAIT_FOR_READY (grpc_static_slice_table[30])
/* "grpc.timeout" */
#define GRPC_MDSTR_GRPC_DOT_TIMEOUT (grpc_static_slice_table[31])
/* "grpc.max_request_message_bytes" */
#define GRPC_MDSTR_GRPC_DOT_MAX_REQUEST_MESSAGE_BYTES (grpc_static_slice_table[32])
/* "grpc.max_response_message_bytes" */
#define GRPC_MDSTR_GRPC_DOT_MAX_RESPONSE_MESSAGE_BYTES (grpc_static_slice_table[33])
/* "/grpc.lb.v1.LoadBalancer/BalanceLoad" */
#define GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD (grpc_static_slice_table[34])
/* "deflate" */
#define GRPC_MDSTR_DEFLATE (grpc_static_slice_table[35])
/* "gzip" */
#define GRPC_MDSTR_GZIP (grpc_static_slice_table[36])
/* "stream/gzip" */
#define GRPC_MDSTR_STREAM_SLASH_GZIP (grpc_static_slice_table[37])
/* "0" */
#define GRPC_MDSTR_0 (grpc_static_slice_table[38])
/* "identity" */
#define GRPC_MDSTR_IDENTITY (grpc_static_slice_table[39])
/* "trailers" */
#define GRPC_MDSTR_TRAILERS (grpc_static_slice_table[40])
/* "application/grpc" */
#define GRPC_MDSTR_APPLICATION_SLASH_GRPC (grpc_static_slice_table[41])
/* "POST" */
#define GRPC_MDSTR_POST (grpc_static_slice_table[42])
/* "200" */
#define GRPC_MDSTR_200 (grpc_static_slice_table[43])
/* "404" */
#define GRPC_MDSTR_404 (grpc_static_slice_table[44])
/* "http" */
#define GRPC_MDSTR_HTTP (grpc_static_slice_table[45])
/* "https" */
#define GRPC_MDSTR_HTTPS (grpc_static_slice_table[46])
/* "grpc" */
#define GRPC_MDSTR_GRPC (grpc_static_slice_table[47])
/* "GET" */
#define GRPC_MDSTR_GET (grpc_static_slice_table[48])
/* "PUT" */
#define GRPC_MDSTR_PUT (grpc_static_slice_table[49])
/* "/" */
#define GRPC_MDSTR_SLASH (grpc_static_slice_table[50])
/* "/index.html" */
#define GRPC_MDSTR_SLASH_INDEX_DOT_HTML (grpc_static_slice_table[51])
/* "204" */
#define GRPC_MDSTR_204 (grpc_static_slice_table[52])
/* "206" */
#define GRPC_MDSTR_206 (grpc_static_slice_table[53])
/* "304" */
#define GRPC_MDSTR_304 (grpc_static_slice_table[54])
/* "400" */
#define GRPC_MDSTR_400 (grpc_static_slice_table[55])
/* "500" */
#define GRPC_MDSTR_500 (grpc_static_slice_table[56])
/* "accept-charset" */
#define GRPC_MDSTR_ACCEPT_CHARSET (grpc_static_slice_table[57])
/* "gzip, deflate" */
#define GRPC_MDSTR_GZIP_COMMA_DEFLATE (grpc_static_slice_table[58])
/* "accept-language" */
#define GRPC_MDSTR_ACCEPT_LANGUAGE (grpc_static_slice_table[59])
/* "accept-ranges" */
#define GRPC_MDSTR_ACCEPT_RANGES (grpc_static_slice_table[60])
/* "accept" */
#define GRPC_MDSTR_ACCEPT (grpc_static_slice_table[61])
/* "access-control-allow-origin" */
#define GRPC_MDSTR_ACCESS_CONTROL_ALLOW_ORIGIN (grpc_static_slice_table[62])
/* "age" */
#define GRPC_MDSTR_AGE (grpc_static_slice_table[63])
/* "allow" */
#define GRPC_MDSTR_ALLOW (grpc_static_slice_table[64])
/* "authorization" */
#define GRPC_MDSTR_AUTHORIZATION (grpc_static_slice_table[65])
/* "cache-control" */
#define GRPC_MDSTR_CACHE_CONTROL (grpc_static_slice_table[66])
/* "content-disposition" */
#define GRPC_MDSTR_CONTENT_DISPOSITION (grpc_static_slice_table[67])
/* "content-language" */
#define GRPC_MDSTR_CONTENT_LANGUAGE (grpc_static_slice_table[68])
/* "content-length" */
#define GRPC_MDSTR_CONTENT_LENGTH (grpc_static_slice_table[69])
/* "content-location" */
#define GRPC_MDSTR_CONTENT_LOCATION (grpc_static_slice_table[70])
/* "content-range" */
#define GRPC_MDSTR_CONTENT_RANGE (grpc_static_slice_table[71])
/* "cookie" */
#define GRPC_MDSTR_COOKIE (grpc_static_slice_table[72])
/* "date" */
#define GRPC_MDSTR_DATE (grpc_static_slice_table[73])
/* "etag" */
#define GRPC_MDSTR_ETAG (grpc_static_slice_table[74])
/* "expect" */
#define GRPC_MDSTR_EXPECT (grpc_static_slice_table[75])
/* "expires" */
#define GRPC_MDSTR_EXPIRES (grpc_static_slice_table[76])
/* "from" */
#define GRPC_MDSTR_FROM (grpc_static_slice_table[77])
/* "if-match" */
#define GRPC_MDSTR_IF_MATCH (grpc_static_slice_table[78])
/* "if-modified-since" */
#define GRPC_MDSTR_IF_MODIFIED_SINCE (grpc_static_slice_table[79])
/* "if-none-match" */
#define GRPC_MDSTR_IF_NONE_MATCH (grpc_static_slice_table[80])
/* "if-range" */
#define GRPC_MDSTR_IF_RANGE (grpc_static_slice_table[81])
/* "if-unmodified-since" */
#define GRPC_MDSTR_IF_UNMODIFIED_SINCE (grpc_static_slice_table[82])
/* "last-modified" */
#define GRPC_MDSTR_LAST_MODIFIED (grpc_static_slice_table[83])
/* "lb-cost-bin" */
#define GRPC_MDSTR_LB_COST_BIN (grpc_static_slice_table[84])
/* "link" */
#define GRPC_MDSTR_LINK (grpc_static_slice_table[85])
/* "location" */
#define GRPC_MDSTR_LOCATION (grpc_static_slice_table[86])
/* "max-forwards" */
#define GRPC_MDSTR_MAX_FORWARDS (grpc_static_slice_table[87])
/* "proxy-authenticate" */
#define GRPC_MDSTR_PROXY_AUTHENTICATE (grpc_static_slice_table[88])
/* "proxy-authorization" */
#define GRPC_MDSTR_PROXY_AUTHORIZATION (grpc_static_slice_table[89])
/* "range" */
#define GRPC_MDSTR_RANGE (grpc_static_slice_table[90])
/* "referer" */
#define GRPC_MDSTR_REFERER (grpc_static_slice_table[91])
/* "refresh" */
#define GRPC_MDSTR_REFRESH (grpc_static_slice_table[92])
/* "retry-after" */
#define GRPC_MDSTR_RETRY_AFTER (grpc_static_slice_table[93])
/* "server" */
#define GRPC_MDSTR_SERVER (grpc_static_slice_table[94])
/* "set-cookie" */
#define GRPC_MDSTR_SET_COOKIE (grpc_static_slice_table[95])
/* "strict-transport-security" */
#define GRPC_MDSTR_STRICT_TRANSPORT_SECURITY (grpc_static_slice_table[96])
/* "transfer-encoding" */
#define GRPC_MDSTR_TRANSFER_ENCODING (grpc_static_slice_table[97])
/* "vary" */
#define GRPC_MDSTR_VARY (grpc_static_slice_table[98])
/* "via" */
#define GRPC_MDSTR_VIA (grpc_static_slice_table[99])
/* "www-authenticate" */
#define GRPC_MDSTR_WWW_AUTHENTICATE (grpc_static_slice_table[100])
/* "identity,deflate" */
#define GRPC_MDSTR_IDENTITY_COMMA_DEFLATE (grpc_static_slice_table[101])
/* "identity,gzip" */
#define GRPC_MDSTR_IDENTITY_COMMA_GZIP (grpc_static_slice_table[102])
/* "deflate,gzip" */
#define GRPC_MDSTR_DEFLATE_COMMA_GZIP (grpc_static_slice_table[103])
/* "identity,deflate,gzip" */
#define GRPC_MDSTR_IDENTITY_COMMA_DEFLATE_COMMA_GZIP (grpc_static_slice_table[104])

extern const grpc_slice_refcount_vtable grpc_static_metadata_vtable;
extern grpc_slice_refcount grpc_static_metadata_refcounts[GRPC_STATIC_MDSTR_COUNT];
#define GRPC_IS_STATIC_METADATA_STRING(slice) \
  ((slice).refcount != NULL && (slice).refcount->vtable == &grpc_static_metadata_vtable)

#define GRPC_STATIC_METADATA_INDEX(static_slice) \
  ((int)((static_slice).refcount - grpc_static_metadata_refcounts))

#define GRPC_STATIC_MDELEM_COUNT 86
extern grpc_mdelem_data grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
extern uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT];
/* "grpc-status": "0" Index="0" */
#define GRPC_MDELEM_GRPC_STATUS_0 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[0], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-status": "1" Index="0" */
#define GRPC_MDELEM_GRPC_STATUS_1 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[1], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-status": "2" Index="0" */
#define GRPC_MDELEM_GRPC_STATUS_2 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[2], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-encoding": "identity" Index="0" */
#define GRPC_MDELEM_GRPC_ENCODING_IDENTITY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[3], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-encoding": "gzip" Index="0" */
#define GRPC_MDELEM_GRPC_ENCODING_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[4], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-encoding": "deflate" Index="0" */
#define GRPC_MDELEM_GRPC_ENCODING_DEFLATE (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[5], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "te": "trailers" Index="0" */
#define GRPC_MDELEM_TE_TRAILERS (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[6], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "content-type": "application/grpc" Index="0" */
#define GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[7], GRPC_MDELEM_STORAGE_STATIC, 0))
/* ":method": "POST" Index="3" */
#define GRPC_MDELEM_METHOD_POST (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[8], GRPC_MDELEM_STORAGE_STATIC, 3))
/* ":status": "200" Index="8" */
#define GRPC_MDELEM_STATUS_200 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[9], GRPC_MDELEM_STORAGE_STATIC, 8))
/* ":status": "404" Index="13" */
#define GRPC_MDELEM_STATUS_404 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[10], GRPC_MDELEM_STORAGE_STATIC, 13))
/* ":scheme": "http" Index="6" */
#define GRPC_MDELEM_SCHEME_HTTP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[11], GRPC_MDELEM_STORAGE_STATIC, 6))
/* ":scheme": "https" Index="7" */
#define GRPC_MDELEM_SCHEME_HTTPS (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[12], GRPC_MDELEM_STORAGE_STATIC, 7))
/* ":scheme": "grpc" Index="0" */
#define GRPC_MDELEM_SCHEME_GRPC (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[13], GRPC_MDELEM_STORAGE_STATIC, 0))
/* ":authority": "" Index="1" */
#define GRPC_MDELEM_AUTHORITY_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[14], GRPC_MDELEM_STORAGE_STATIC, 1))
/* ":method": "GET" Index="2" */
#define GRPC_MDELEM_METHOD_GET (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[15], GRPC_MDELEM_STORAGE_STATIC, 2))
/* ":method": "PUT" Index="0" */
#define GRPC_MDELEM_METHOD_PUT (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[16], GRPC_MDELEM_STORAGE_STATIC, 0))
/* ":path": "/" Index="4" */
#define GRPC_MDELEM_PATH_SLASH (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[17], GRPC_MDELEM_STORAGE_STATIC, 4))
/* ":path": "/index.html" Index="5" */
#define GRPC_MDELEM_PATH_SLASH_INDEX_DOT_HTML (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[18], GRPC_MDELEM_STORAGE_STATIC, 5))
/* ":status": "204" Index="9" */
#define GRPC_MDELEM_STATUS_204 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[19], GRPC_MDELEM_STORAGE_STATIC, 9))
/* ":status": "206" Index="10" */
#define GRPC_MDELEM_STATUS_206 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[20], GRPC_MDELEM_STORAGE_STATIC, 10))
/* ":status": "304" Index="11" */
#define GRPC_MDELEM_STATUS_304 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[21], GRPC_MDELEM_STORAGE_STATIC, 11))
/* ":status": "400" Index="12" */
#define GRPC_MDELEM_STATUS_400 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[22], GRPC_MDELEM_STORAGE_STATIC, 12))
/* ":status": "500" Index="14" */
#define GRPC_MDELEM_STATUS_500 (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[23], GRPC_MDELEM_STORAGE_STATIC, 14))
/* "accept-charset": "" Index="15" */
#define GRPC_MDELEM_ACCEPT_CHARSET_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[24], GRPC_MDELEM_STORAGE_STATIC, 15))
/* "accept-encoding": "" Index="0" */
#define GRPC_MDELEM_ACCEPT_ENCODING_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[25], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "accept-encoding": "gzip, deflate" Index="16" */
#define GRPC_MDELEM_ACCEPT_ENCODING_GZIP_COMMA_DEFLATE (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[26], GRPC_MDELEM_STORAGE_STATIC, 16))
/* "accept-language": "" Index="17" */
#define GRPC_MDELEM_ACCEPT_LANGUAGE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[27], GRPC_MDELEM_STORAGE_STATIC, 17))
/* "accept-ranges": "" Index="18" */
#define GRPC_MDELEM_ACCEPT_RANGES_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[28], GRPC_MDELEM_STORAGE_STATIC, 18))
/* "accept": "" Index="19" */
#define GRPC_MDELEM_ACCEPT_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[29], GRPC_MDELEM_STORAGE_STATIC, 19))
/* "access-control-allow-origin": "" Index="20" */
#define GRPC_MDELEM_ACCESS_CONTROL_ALLOW_ORIGIN_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[30], GRPC_MDELEM_STORAGE_STATIC, 20))
/* "age": "" Index="21" */
#define GRPC_MDELEM_AGE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[31], GRPC_MDELEM_STORAGE_STATIC, 21))
/* "allow": "" Index="22" */
#define GRPC_MDELEM_ALLOW_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[32], GRPC_MDELEM_STORAGE_STATIC, 22))
/* "authorization": "" Index="23" */
#define GRPC_MDELEM_AUTHORIZATION_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[33], GRPC_MDELEM_STORAGE_STATIC, 23))
/* "cache-control": "" Index="24" */
#define GRPC_MDELEM_CACHE_CONTROL_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[34], GRPC_MDELEM_STORAGE_STATIC, 24))
/* "content-disposition": "" Index="25" */
#define GRPC_MDELEM_CONTENT_DISPOSITION_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[35], GRPC_MDELEM_STORAGE_STATIC, 25))
/* "content-encoding": "identity" Index="0" */
#define GRPC_MDELEM_CONTENT_ENCODING_IDENTITY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[36], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "content-encoding": "gzip" Index="0" */
#define GRPC_MDELEM_CONTENT_ENCODING_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[37], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "content-encoding": "" Index="26" */
#define GRPC_MDELEM_CONTENT_ENCODING_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[38], GRPC_MDELEM_STORAGE_STATIC, 26))
/* "content-language": "" Index="27" */
#define GRPC_MDELEM_CONTENT_LANGUAGE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[39], GRPC_MDELEM_STORAGE_STATIC, 27))
/* "content-length": "" Index="28" */
#define GRPC_MDELEM_CONTENT_LENGTH_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[40], GRPC_MDELEM_STORAGE_STATIC, 28))
/* "content-location": "" Index="29" */
#define GRPC_MDELEM_CONTENT_LOCATION_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[41], GRPC_MDELEM_STORAGE_STATIC, 29))
/* "content-range": "" Index="30" */
#define GRPC_MDELEM_CONTENT_RANGE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[42], GRPC_MDELEM_STORAGE_STATIC, 30))
/* "content-type": "" Index="31" */
#define GRPC_MDELEM_CONTENT_TYPE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[43], GRPC_MDELEM_STORAGE_STATIC, 31))
/* "cookie": "" Index="32" */
#define GRPC_MDELEM_COOKIE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[44], GRPC_MDELEM_STORAGE_STATIC, 32))
/* "date": "" Index="33" */
#define GRPC_MDELEM_DATE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[45], GRPC_MDELEM_STORAGE_STATIC, 33))
/* "etag": "" Index="34" */
#define GRPC_MDELEM_ETAG_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[46], GRPC_MDELEM_STORAGE_STATIC, 34))
/* "expect": "" Index="35" */
#define GRPC_MDELEM_EXPECT_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[47], GRPC_MDELEM_STORAGE_STATIC, 35))
/* "expires": "" Index="36" */
#define GRPC_MDELEM_EXPIRES_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[48], GRPC_MDELEM_STORAGE_STATIC, 36))
/* "from": "" Index="37" */
#define GRPC_MDELEM_FROM_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[49], GRPC_MDELEM_STORAGE_STATIC, 37))
/* "host": "" Index="38" */
#define GRPC_MDELEM_HOST_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[50], GRPC_MDELEM_STORAGE_STATIC, 38))
/* "if-match": "" Index="39" */
#define GRPC_MDELEM_IF_MATCH_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[51], GRPC_MDELEM_STORAGE_STATIC, 39))
/* "if-modified-since": "" Index="40" */
#define GRPC_MDELEM_IF_MODIFIED_SINCE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[52], GRPC_MDELEM_STORAGE_STATIC, 40))
/* "if-none-match": "" Index="41" */
#define GRPC_MDELEM_IF_NONE_MATCH_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[53], GRPC_MDELEM_STORAGE_STATIC, 41))
/* "if-range": "" Index="42" */
#define GRPC_MDELEM_IF_RANGE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[54], GRPC_MDELEM_STORAGE_STATIC, 42))
/* "if-unmodified-since": "" Index="43" */
#define GRPC_MDELEM_IF_UNMODIFIED_SINCE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[55], GRPC_MDELEM_STORAGE_STATIC, 43))
/* "last-modified": "" Index="44" */
#define GRPC_MDELEM_LAST_MODIFIED_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[56], GRPC_MDELEM_STORAGE_STATIC, 44))
/* "lb-token": "" Index="0" */
#define GRPC_MDELEM_LB_TOKEN_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[57], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "lb-cost-bin": "" Index="0" */
#define GRPC_MDELEM_LB_COST_BIN_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[58], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "link": "" Index="45" */
#define GRPC_MDELEM_LINK_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[59], GRPC_MDELEM_STORAGE_STATIC, 45))
/* "location": "" Index="46" */
#define GRPC_MDELEM_LOCATION_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[60], GRPC_MDELEM_STORAGE_STATIC, 46))
/* "max-forwards": "" Index="47" */
#define GRPC_MDELEM_MAX_FORWARDS_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[61], GRPC_MDELEM_STORAGE_STATIC, 47))
/* "proxy-authenticate": "" Index="48" */
#define GRPC_MDELEM_PROXY_AUTHENTICATE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[62], GRPC_MDELEM_STORAGE_STATIC, 48))
/* "proxy-authorization": "" Index="49" */
#define GRPC_MDELEM_PROXY_AUTHORIZATION_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[63], GRPC_MDELEM_STORAGE_STATIC, 49))
/* "range": "" Index="50" */
#define GRPC_MDELEM_RANGE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[64], GRPC_MDELEM_STORAGE_STATIC, 50))
/* "referer": "" Index="51" */
#define GRPC_MDELEM_REFERER_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[65], GRPC_MDELEM_STORAGE_STATIC, 51))
/* "refresh": "" Index="52" */
#define GRPC_MDELEM_REFRESH_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[66], GRPC_MDELEM_STORAGE_STATIC, 52))
/* "retry-after": "" Index="53" */
#define GRPC_MDELEM_RETRY_AFTER_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[67], GRPC_MDELEM_STORAGE_STATIC, 53))
/* "server": "" Index="54" */
#define GRPC_MDELEM_SERVER_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[68], GRPC_MDELEM_STORAGE_STATIC, 54))
/* "set-cookie": "" Index="55" */
#define GRPC_MDELEM_SET_COOKIE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[69], GRPC_MDELEM_STORAGE_STATIC, 55))
/* "strict-transport-security": "" Index="56" */
#define GRPC_MDELEM_STRICT_TRANSPORT_SECURITY_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[70], GRPC_MDELEM_STORAGE_STATIC, 56))
/* "transfer-encoding": "" Index="57" */
#define GRPC_MDELEM_TRANSFER_ENCODING_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[71], GRPC_MDELEM_STORAGE_STATIC, 57))
/* "user-agent": "" Index="58" */
#define GRPC_MDELEM_USER_AGENT_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[72], GRPC_MDELEM_STORAGE_STATIC, 58))
/* "vary": "" Index="59" */
#define GRPC_MDELEM_VARY_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[73], GRPC_MDELEM_STORAGE_STATIC, 59))
/* "via": "" Index="60" */
#define GRPC_MDELEM_VIA_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[74], GRPC_MDELEM_STORAGE_STATIC, 60))
/* "www-authenticate": "" Index="61" */
#define GRPC_MDELEM_WWW_AUTHENTICATE_EMPTY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[75], GRPC_MDELEM_STORAGE_STATIC, 61))
/* "grpc-accept-encoding": "identity" Index="0" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[76], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-accept-encoding": "deflate" Index="0" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[77], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-accept-encoding": "identity,deflate" Index="0" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[78], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-accept-encoding": "gzip" Index="0" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[79], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-accept-encoding": "identity,gzip" Index="0" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[80], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-accept-encoding": "deflate,gzip" Index="0" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE_COMMA_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[81], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "grpc-accept-encoding": "identity,deflate,gzip" Index="0" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[82], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "accept-encoding": "identity" Index="0" */
#define GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[83], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "accept-encoding": "gzip" Index="0" */
#define GRPC_MDELEM_ACCEPT_ENCODING_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[84], GRPC_MDELEM_STORAGE_STATIC, 0))
/* "accept-encoding": "identity,gzip" Index="0" */
#define GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[85], GRPC_MDELEM_STORAGE_STATIC, 0))

grpc_mdelem grpc_static_mdelem_for_static_strings(int a, int b);
typedef enum {
  GRPC_BATCH_PATH,
  GRPC_BATCH_METHOD,
  GRPC_BATCH_STATUS,
  GRPC_BATCH_AUTHORITY,
  GRPC_BATCH_SCHEME,
  GRPC_BATCH_TE,
  GRPC_BATCH_GRPC_MESSAGE,
  GRPC_BATCH_GRPC_STATUS,
  GRPC_BATCH_GRPC_PAYLOAD_BIN,
  GRPC_BATCH_GRPC_ENCODING,
  GRPC_BATCH_GRPC_ACCEPT_ENCODING,
  GRPC_BATCH_GRPC_SERVER_STATS_BIN,
  GRPC_BATCH_GRPC_TAGS_BIN,
  GRPC_BATCH_GRPC_TRACE_BIN,
  GRPC_BATCH_CONTENT_TYPE,
  GRPC_BATCH_CONTENT_ENCODING,
  GRPC_BATCH_ACCEPT_ENCODING,
  GRPC_BATCH_GRPC_INTERNAL_ENCODING_REQUEST,
  GRPC_BATCH_GRPC_INTERNAL_STREAM_ENCODING_REQUEST,
  GRPC_BATCH_USER_AGENT,
  GRPC_BATCH_HOST,
  GRPC_BATCH_LB_TOKEN,
  GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS,
  GRPC_BATCH_GRPC_RETRY_PUSHBACK_MS,
  GRPC_BATCH_CALLOUTS_COUNT
} grpc_metadata_batch_callouts_index;

typedef union {
  struct grpc_linked_mdelem *array[GRPC_BATCH_CALLOUTS_COUNT];
  struct {
  struct grpc_linked_mdelem *path;
  struct grpc_linked_mdelem *method;
  struct grpc_linked_mdelem *status;
  struct grpc_linked_mdelem *authority;
  struct grpc_linked_mdelem *scheme;
  struct grpc_linked_mdelem *te;
  struct grpc_linked_mdelem *grpc_message;
  struct grpc_linked_mdelem *grpc_status;
  struct grpc_linked_mdelem *grpc_payload_bin;
  struct grpc_linked_mdelem *grpc_encoding;
  struct grpc_linked_mdelem *grpc_accept_encoding;
  struct grpc_linked_mdelem *grpc_server_stats_bin;
  struct grpc_linked_mdelem *grpc_tags_bin;
  struct grpc_linked_mdelem *grpc_trace_bin;
  struct grpc_linked_mdelem *content_type;
  struct grpc_linked_mdelem *content_encoding;
  struct grpc_linked_mdelem *accept_encoding;
  struct grpc_linked_mdelem *grpc_internal_encoding_request;
  struct grpc_linked_mdelem *grpc_internal_stream_encoding_request;
  struct grpc_linked_mdelem *user_agent;
  struct grpc_linked_mdelem *host;
  struct grpc_linked_mdelem *lb_token;
  struct grpc_linked_mdelem *grpc_previous_rpc_attempts;
  struct grpc_linked_mdelem *grpc_retry_pushback_ms;
  } named;
} grpc_metadata_batch_callouts;

#define GRPC_BATCH_INDEX_OF(slice) \
  (GRPC_IS_STATIC_METADATA_STRING((slice)) ? (grpc_metadata_batch_callouts_index)GPR_CLAMP(GRPC_STATIC_METADATA_INDEX((slice)), 0, GRPC_BATCH_CALLOUTS_COUNT) : GRPC_BATCH_CALLOUTS_COUNT)

extern bool grpc_static_callout_is_default[GRPC_BATCH_CALLOUTS_COUNT];

extern const uint8_t grpc_static_accept_encoding_metadata[8];
#define GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(algs) (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[grpc_static_accept_encoding_metadata[(algs)]], GRPC_MDELEM_STORAGE_STATIC, 0))

extern const uint8_t grpc_static_accept_stream_encoding_metadata[4];
#define GRPC_MDELEM_ACCEPT_STREAM_ENCODING_FOR_ALGORITHMS(algs) (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[grpc_static_accept_stream_encoding_metadata[(algs)]], GRPC_MDELEM_STORAGE_STATIC, 0))
#endif /* GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H */
