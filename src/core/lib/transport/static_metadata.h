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

#include <grpc/support/port_platform.h>

#include <cstdint>

#include "src/core/lib/slice/static_slice.h"
#include "src/core/lib/transport/metadata.h"

#define GRPC_STATIC_MDELEM_COUNT 84

namespace grpc_core {
extern StaticMetadata g_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
extern grpc_mdelem g_static_mdelem_manifested[GRPC_STATIC_MDELEM_COUNT];
}  // namespace grpc_core

extern uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT];
/* ":authority": "" */
#define GRPC_MDELEM_AUTHORITY_EMPTY (::grpc_core::g_static_mdelem_manifested[0])
/* ":method": "GET" */
#define GRPC_MDELEM_METHOD_GET (::grpc_core::g_static_mdelem_manifested[1])
/* ":method": "POST" */
#define GRPC_MDELEM_METHOD_POST (::grpc_core::g_static_mdelem_manifested[2])
/* ":path": "/" */
#define GRPC_MDELEM_PATH_SLASH (::grpc_core::g_static_mdelem_manifested[3])
/* ":path": "/index.html" */
#define GRPC_MDELEM_PATH_SLASH_INDEX_DOT_HTML \
  (::grpc_core::g_static_mdelem_manifested[4])
/* ":scheme": "http" */
#define GRPC_MDELEM_SCHEME_HTTP (::grpc_core::g_static_mdelem_manifested[5])
/* ":scheme": "https" */
#define GRPC_MDELEM_SCHEME_HTTPS (::grpc_core::g_static_mdelem_manifested[6])
/* ":status": "200" */
#define GRPC_MDELEM_STATUS_200 (::grpc_core::g_static_mdelem_manifested[7])
/* ":status": "204" */
#define GRPC_MDELEM_STATUS_204 (::grpc_core::g_static_mdelem_manifested[8])
/* ":status": "206" */
#define GRPC_MDELEM_STATUS_206 (::grpc_core::g_static_mdelem_manifested[9])
/* ":status": "304" */
#define GRPC_MDELEM_STATUS_304 (::grpc_core::g_static_mdelem_manifested[10])
/* ":status": "400" */
#define GRPC_MDELEM_STATUS_400 (::grpc_core::g_static_mdelem_manifested[11])
/* ":status": "404" */
#define GRPC_MDELEM_STATUS_404 (::grpc_core::g_static_mdelem_manifested[12])
/* ":status": "500" */
#define GRPC_MDELEM_STATUS_500 (::grpc_core::g_static_mdelem_manifested[13])
/* "accept-charset": "" */
#define GRPC_MDELEM_ACCEPT_CHARSET_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[14])
/* "accept-encoding": "gzip, deflate" */
#define GRPC_MDELEM_ACCEPT_ENCODING_GZIP_COMMA_DEFLATE \
  (::grpc_core::g_static_mdelem_manifested[15])
/* "accept-language": "" */
#define GRPC_MDELEM_ACCEPT_LANGUAGE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[16])
/* "accept-ranges": "" */
#define GRPC_MDELEM_ACCEPT_RANGES_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[17])
/* "accept": "" */
#define GRPC_MDELEM_ACCEPT_EMPTY (::grpc_core::g_static_mdelem_manifested[18])
/* "access-control-allow-origin": "" */
#define GRPC_MDELEM_ACCESS_CONTROL_ALLOW_ORIGIN_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[19])
/* "age": "" */
#define GRPC_MDELEM_AGE_EMPTY (::grpc_core::g_static_mdelem_manifested[20])
/* "allow": "" */
#define GRPC_MDELEM_ALLOW_EMPTY (::grpc_core::g_static_mdelem_manifested[21])
/* "authorization": "" */
#define GRPC_MDELEM_AUTHORIZATION_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[22])
/* "cache-control": "" */
#define GRPC_MDELEM_CACHE_CONTROL_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[23])
/* "content-disposition": "" */
#define GRPC_MDELEM_CONTENT_DISPOSITION_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[24])
/* "content-encoding": "" */
#define GRPC_MDELEM_CONTENT_ENCODING_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[25])
/* "content-language": "" */
#define GRPC_MDELEM_CONTENT_LANGUAGE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[26])
/* "content-length": "" */
#define GRPC_MDELEM_CONTENT_LENGTH_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[27])
/* "content-location": "" */
#define GRPC_MDELEM_CONTENT_LOCATION_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[28])
/* "content-range": "" */
#define GRPC_MDELEM_CONTENT_RANGE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[29])
/* "content-type": "" */
#define GRPC_MDELEM_CONTENT_TYPE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[30])
/* "cookie": "" */
#define GRPC_MDELEM_COOKIE_EMPTY (::grpc_core::g_static_mdelem_manifested[31])
/* "date": "" */
#define GRPC_MDELEM_DATE_EMPTY (::grpc_core::g_static_mdelem_manifested[32])
/* "etag": "" */
#define GRPC_MDELEM_ETAG_EMPTY (::grpc_core::g_static_mdelem_manifested[33])
/* "expect": "" */
#define GRPC_MDELEM_EXPECT_EMPTY (::grpc_core::g_static_mdelem_manifested[34])
/* "expires": "" */
#define GRPC_MDELEM_EXPIRES_EMPTY (::grpc_core::g_static_mdelem_manifested[35])
/* "from": "" */
#define GRPC_MDELEM_FROM_EMPTY (::grpc_core::g_static_mdelem_manifested[36])
/* "host": "" */
#define GRPC_MDELEM_HOST_EMPTY (::grpc_core::g_static_mdelem_manifested[37])
/* "if-match": "" */
#define GRPC_MDELEM_IF_MATCH_EMPTY (::grpc_core::g_static_mdelem_manifested[38])
/* "if-modified-since": "" */
#define GRPC_MDELEM_IF_MODIFIED_SINCE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[39])
/* "if-none-match": "" */
#define GRPC_MDELEM_IF_NONE_MATCH_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[40])
/* "if-range": "" */
#define GRPC_MDELEM_IF_RANGE_EMPTY (::grpc_core::g_static_mdelem_manifested[41])
/* "if-unmodified-since": "" */
#define GRPC_MDELEM_IF_UNMODIFIED_SINCE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[42])
/* "last-modified": "" */
#define GRPC_MDELEM_LAST_MODIFIED_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[43])
/* "link": "" */
#define GRPC_MDELEM_LINK_EMPTY (::grpc_core::g_static_mdelem_manifested[44])
/* "location": "" */
#define GRPC_MDELEM_LOCATION_EMPTY (::grpc_core::g_static_mdelem_manifested[45])
/* "max-forwards": "" */
#define GRPC_MDELEM_MAX_FORWARDS_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[46])
/* "proxy-authenticate": "" */
#define GRPC_MDELEM_PROXY_AUTHENTICATE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[47])
/* "proxy-authorization": "" */
#define GRPC_MDELEM_PROXY_AUTHORIZATION_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[48])
/* "range": "" */
#define GRPC_MDELEM_RANGE_EMPTY (::grpc_core::g_static_mdelem_manifested[49])
/* "referer": "" */
#define GRPC_MDELEM_REFERER_EMPTY (::grpc_core::g_static_mdelem_manifested[50])
/* "refresh": "" */
#define GRPC_MDELEM_REFRESH_EMPTY (::grpc_core::g_static_mdelem_manifested[51])
/* "retry-after": "" */
#define GRPC_MDELEM_RETRY_AFTER_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[52])
/* "server": "" */
#define GRPC_MDELEM_SERVER_EMPTY (::grpc_core::g_static_mdelem_manifested[53])
/* "set-cookie": "" */
#define GRPC_MDELEM_SET_COOKIE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[54])
/* "strict-transport-security": "" */
#define GRPC_MDELEM_STRICT_TRANSPORT_SECURITY_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[55])
/* "transfer-encoding": "" */
#define GRPC_MDELEM_TRANSFER_ENCODING_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[56])
/* "user-agent": "" */
#define GRPC_MDELEM_USER_AGENT_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[57])
/* "vary": "" */
#define GRPC_MDELEM_VARY_EMPTY (::grpc_core::g_static_mdelem_manifested[58])
/* "via": "" */
#define GRPC_MDELEM_VIA_EMPTY (::grpc_core::g_static_mdelem_manifested[59])
/* "www-authenticate": "" */
#define GRPC_MDELEM_WWW_AUTHENTICATE_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[60])
/* "grpc-status": "0" */
#define GRPC_MDELEM_GRPC_STATUS_0 (::grpc_core::g_static_mdelem_manifested[61])
/* "grpc-status": "1" */
#define GRPC_MDELEM_GRPC_STATUS_1 (::grpc_core::g_static_mdelem_manifested[62])
/* "grpc-status": "2" */
#define GRPC_MDELEM_GRPC_STATUS_2 (::grpc_core::g_static_mdelem_manifested[63])
/* "grpc-encoding": "identity" */
#define GRPC_MDELEM_GRPC_ENCODING_IDENTITY \
  (::grpc_core::g_static_mdelem_manifested[64])
/* "grpc-encoding": "gzip" */
#define GRPC_MDELEM_GRPC_ENCODING_GZIP \
  (::grpc_core::g_static_mdelem_manifested[65])
/* "grpc-encoding": "deflate" */
#define GRPC_MDELEM_GRPC_ENCODING_DEFLATE \
  (::grpc_core::g_static_mdelem_manifested[66])
/* "content-type": "application/grpc" */
#define GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC \
  (::grpc_core::g_static_mdelem_manifested[67])
/* ":scheme": "grpc" */
#define GRPC_MDELEM_SCHEME_GRPC (::grpc_core::g_static_mdelem_manifested[68])
/* ":method": "PUT" */
#define GRPC_MDELEM_METHOD_PUT (::grpc_core::g_static_mdelem_manifested[69])
/* "accept-encoding": "" */
#define GRPC_MDELEM_ACCEPT_ENCODING_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[70])
/* "content-encoding": "identity" */
#define GRPC_MDELEM_CONTENT_ENCODING_IDENTITY \
  (::grpc_core::g_static_mdelem_manifested[71])
/* "content-encoding": "gzip" */
#define GRPC_MDELEM_CONTENT_ENCODING_GZIP \
  (::grpc_core::g_static_mdelem_manifested[72])
/* "lb-cost-bin": "" */
#define GRPC_MDELEM_LB_COST_BIN_EMPTY \
  (::grpc_core::g_static_mdelem_manifested[73])
/* "grpc-accept-encoding": "identity" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY \
  (::grpc_core::g_static_mdelem_manifested[74])
/* "grpc-accept-encoding": "deflate" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE \
  (::grpc_core::g_static_mdelem_manifested[75])
/* "grpc-accept-encoding": "identity,deflate" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE \
  (::grpc_core::g_static_mdelem_manifested[76])
/* "grpc-accept-encoding": "gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_GZIP \
  (::grpc_core::g_static_mdelem_manifested[77])
/* "grpc-accept-encoding": "identity,gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP \
  (::grpc_core::g_static_mdelem_manifested[78])
/* "grpc-accept-encoding": "deflate,gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE_COMMA_GZIP \
  (::grpc_core::g_static_mdelem_manifested[79])
/* "grpc-accept-encoding": "identity,deflate,gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP \
  (::grpc_core::g_static_mdelem_manifested[80])
/* "accept-encoding": "identity" */
#define GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY \
  (::grpc_core::g_static_mdelem_manifested[81])
/* "accept-encoding": "gzip" */
#define GRPC_MDELEM_ACCEPT_ENCODING_GZIP \
  (::grpc_core::g_static_mdelem_manifested[82])
/* "accept-encoding": "identity,gzip" */
#define GRPC_MDELEM_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP \
  (::grpc_core::g_static_mdelem_manifested[83])

grpc_mdelem grpc_static_mdelem_for_static_strings(intptr_t a, intptr_t b);
typedef enum {
  GRPC_BATCH_PATH,
  GRPC_BATCH_METHOD,
  GRPC_BATCH_STATUS,
  GRPC_BATCH_AUTHORITY,
  GRPC_BATCH_SCHEME,
  GRPC_BATCH_GRPC_STATUS,
  GRPC_BATCH_GRPC_ENCODING,
  GRPC_BATCH_GRPC_ACCEPT_ENCODING,
  GRPC_BATCH_CONTENT_TYPE,
  GRPC_BATCH_CONTENT_ENCODING,
  GRPC_BATCH_ACCEPT_ENCODING,
  GRPC_BATCH_GRPC_INTERNAL_ENCODING_REQUEST,
  GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS,
  GRPC_BATCH_GRPC_RETRY_PUSHBACK_MS,
  GRPC_BATCH_CALLOUTS_COUNT
} grpc_metadata_batch_callouts_index;

typedef union {
  struct grpc_linked_mdelem* array[GRPC_BATCH_CALLOUTS_COUNT];
  struct {
    struct grpc_linked_mdelem* path;
    struct grpc_linked_mdelem* method;
    struct grpc_linked_mdelem* status;
    struct grpc_linked_mdelem* authority;
    struct grpc_linked_mdelem* scheme;
    struct grpc_linked_mdelem* grpc_status;
    struct grpc_linked_mdelem* grpc_encoding;
    struct grpc_linked_mdelem* grpc_accept_encoding;
    struct grpc_linked_mdelem* content_type;
    struct grpc_linked_mdelem* content_encoding;
    struct grpc_linked_mdelem* accept_encoding;
    struct grpc_linked_mdelem* grpc_internal_encoding_request;
    struct grpc_linked_mdelem* grpc_previous_rpc_attempts;
    struct grpc_linked_mdelem* grpc_retry_pushback_ms;
  } named;
} grpc_metadata_batch_callouts;

#define GRPC_BATCH_INDEX_OF(slice)                                             \
  (GRPC_IS_STATIC_METADATA_STRING((slice)) &&                                  \
           reinterpret_cast<grpc_core::StaticSliceRefcount*>((slice).refcount) \
                   ->index <= static_cast<uint32_t>(GRPC_BATCH_CALLOUTS_COUNT) \
       ? static_cast<grpc_metadata_batch_callouts_index>(                      \
             reinterpret_cast<grpc_core::StaticSliceRefcount*>(                \
                 (slice).refcount)                                             \
                 ->index)                                                      \
       : GRPC_BATCH_CALLOUTS_COUNT)

extern const uint8_t grpc_static_accept_encoding_metadata[8];
#define GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(algs)                \
  (GRPC_MAKE_MDELEM(&grpc_core::g_static_mdelem_table                   \
                         [grpc_static_accept_encoding_metadata[(algs)]] \
                             .data(),                                   \
                    GRPC_MDELEM_STORAGE_STATIC))

extern const uint8_t grpc_static_accept_stream_encoding_metadata[4];
#define GRPC_MDELEM_ACCEPT_STREAM_ENCODING_FOR_ALGORITHMS(algs)                \
  (GRPC_MAKE_MDELEM(&grpc_core::g_static_mdelem_table                          \
                         [grpc_static_accept_stream_encoding_metadata[(algs)]] \
                             .data(),                                          \
                    GRPC_MDELEM_STORAGE_STATIC))
#endif /* GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H */
