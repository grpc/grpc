/*
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * WARNING: Auto-generated code.
 *
 * To make changes to this file, change
 * tools/codegen/core/gen_static_metadata.py, and then re-run it.
 *
 * See metadata.h for an explanation of the interface here, and metadata.c for
 * an explanation of what's going on.
 */

#ifndef GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H
#define GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H

#include "src/core/lib/transport/metadata.h"

#define GRPC_STATIC_MDSTR_COUNT 93
extern grpc_mdstr grpc_static_mdstr_table[GRPC_STATIC_MDSTR_COUNT];
/* "0" */
#define GRPC_MDSTR_0 (&grpc_static_mdstr_table[0])
/* "1" */
#define GRPC_MDSTR_1 (&grpc_static_mdstr_table[1])
/* "2" */
#define GRPC_MDSTR_2 (&grpc_static_mdstr_table[2])
/* "200" */
#define GRPC_MDSTR_200 (&grpc_static_mdstr_table[3])
/* "204" */
#define GRPC_MDSTR_204 (&grpc_static_mdstr_table[4])
/* "206" */
#define GRPC_MDSTR_206 (&grpc_static_mdstr_table[5])
/* "304" */
#define GRPC_MDSTR_304 (&grpc_static_mdstr_table[6])
/* "400" */
#define GRPC_MDSTR_400 (&grpc_static_mdstr_table[7])
/* "404" */
#define GRPC_MDSTR_404 (&grpc_static_mdstr_table[8])
/* "500" */
#define GRPC_MDSTR_500 (&grpc_static_mdstr_table[9])
/* "accept" */
#define GRPC_MDSTR_ACCEPT (&grpc_static_mdstr_table[10])
/* "accept-charset" */
#define GRPC_MDSTR_ACCEPT_CHARSET (&grpc_static_mdstr_table[11])
/* "accept-encoding" */
#define GRPC_MDSTR_ACCEPT_ENCODING (&grpc_static_mdstr_table[12])
/* "accept-language" */
#define GRPC_MDSTR_ACCEPT_LANGUAGE (&grpc_static_mdstr_table[13])
/* "accept-ranges" */
#define GRPC_MDSTR_ACCEPT_RANGES (&grpc_static_mdstr_table[14])
/* "access-control-allow-origin" */
#define GRPC_MDSTR_ACCESS_CONTROL_ALLOW_ORIGIN (&grpc_static_mdstr_table[15])
/* "age" */
#define GRPC_MDSTR_AGE (&grpc_static_mdstr_table[16])
/* "allow" */
#define GRPC_MDSTR_ALLOW (&grpc_static_mdstr_table[17])
/* "application/grpc" */
#define GRPC_MDSTR_APPLICATION_SLASH_GRPC (&grpc_static_mdstr_table[18])
/* ":authority" */
#define GRPC_MDSTR_AUTHORITY (&grpc_static_mdstr_table[19])
/* "authorization" */
#define GRPC_MDSTR_AUTHORIZATION (&grpc_static_mdstr_table[20])
/* "cache-control" */
#define GRPC_MDSTR_CACHE_CONTROL (&grpc_static_mdstr_table[21])
/* "content-disposition" */
#define GRPC_MDSTR_CONTENT_DISPOSITION (&grpc_static_mdstr_table[22])
/* "content-encoding" */
#define GRPC_MDSTR_CONTENT_ENCODING (&grpc_static_mdstr_table[23])
/* "content-language" */
#define GRPC_MDSTR_CONTENT_LANGUAGE (&grpc_static_mdstr_table[24])
/* "content-length" */
#define GRPC_MDSTR_CONTENT_LENGTH (&grpc_static_mdstr_table[25])
/* "content-location" */
#define GRPC_MDSTR_CONTENT_LOCATION (&grpc_static_mdstr_table[26])
/* "content-range" */
#define GRPC_MDSTR_CONTENT_RANGE (&grpc_static_mdstr_table[27])
/* "content-type" */
#define GRPC_MDSTR_CONTENT_TYPE (&grpc_static_mdstr_table[28])
/* "cookie" */
#define GRPC_MDSTR_COOKIE (&grpc_static_mdstr_table[29])
/* "date" */
#define GRPC_MDSTR_DATE (&grpc_static_mdstr_table[30])
/* "deflate" */
#define GRPC_MDSTR_DEFLATE (&grpc_static_mdstr_table[31])
/* "deflate,gzip" */
#define GRPC_MDSTR_DEFLATE_COMMA_GZIP (&grpc_static_mdstr_table[32])
/* "" */
#define GRPC_MDSTR_EMPTY (&grpc_static_mdstr_table[33])
/* "etag" */
#define GRPC_MDSTR_ETAG (&grpc_static_mdstr_table[34])
/* "expect" */
#define GRPC_MDSTR_EXPECT (&grpc_static_mdstr_table[35])
/* "expires" */
#define GRPC_MDSTR_EXPIRES (&grpc_static_mdstr_table[36])
/* "from" */
#define GRPC_MDSTR_FROM (&grpc_static_mdstr_table[37])
/* "GET" */
#define GRPC_MDSTR_GET (&grpc_static_mdstr_table[38])
/* "grpc" */
#define GRPC_MDSTR_GRPC (&grpc_static_mdstr_table[39])
/* "grpc-accept-encoding" */
#define GRPC_MDSTR_GRPC_ACCEPT_ENCODING (&grpc_static_mdstr_table[40])
/* "grpc-encoding" */
#define GRPC_MDSTR_GRPC_ENCODING (&grpc_static_mdstr_table[41])
/* "grpc-internal-encoding-request" */
#define GRPC_MDSTR_GRPC_INTERNAL_ENCODING_REQUEST (&grpc_static_mdstr_table[42])
/* "grpc-message" */
#define GRPC_MDSTR_GRPC_MESSAGE (&grpc_static_mdstr_table[43])
/* "grpc-payload-bin" */
#define GRPC_MDSTR_GRPC_PAYLOAD_BIN (&grpc_static_mdstr_table[44])
/* "grpc-stats-bin" */
#define GRPC_MDSTR_GRPC_STATS_BIN (&grpc_static_mdstr_table[45])
/* "grpc-status" */
#define GRPC_MDSTR_GRPC_STATUS (&grpc_static_mdstr_table[46])
/* "grpc-timeout" */
#define GRPC_MDSTR_GRPC_TIMEOUT (&grpc_static_mdstr_table[47])
/* "grpc-tracing-bin" */
#define GRPC_MDSTR_GRPC_TRACING_BIN (&grpc_static_mdstr_table[48])
/* "gzip" */
#define GRPC_MDSTR_GZIP (&grpc_static_mdstr_table[49])
/* "gzip, deflate" */
#define GRPC_MDSTR_GZIP_COMMA_DEFLATE (&grpc_static_mdstr_table[50])
/* "host" */
#define GRPC_MDSTR_HOST (&grpc_static_mdstr_table[51])
/* "http" */
#define GRPC_MDSTR_HTTP (&grpc_static_mdstr_table[52])
/* "https" */
#define GRPC_MDSTR_HTTPS (&grpc_static_mdstr_table[53])
/* "identity" */
#define GRPC_MDSTR_IDENTITY (&grpc_static_mdstr_table[54])
/* "identity,deflate" */
#define GRPC_MDSTR_IDENTITY_COMMA_DEFLATE (&grpc_static_mdstr_table[55])
/* "identity,deflate,gzip" */
#define GRPC_MDSTR_IDENTITY_COMMA_DEFLATE_COMMA_GZIP \
  (&grpc_static_mdstr_table[56])
/* "identity,gzip" */
#define GRPC_MDSTR_IDENTITY_COMMA_GZIP (&grpc_static_mdstr_table[57])
/* "if-match" */
#define GRPC_MDSTR_IF_MATCH (&grpc_static_mdstr_table[58])
/* "if-modified-since" */
#define GRPC_MDSTR_IF_MODIFIED_SINCE (&grpc_static_mdstr_table[59])
/* "if-none-match" */
#define GRPC_MDSTR_IF_NONE_MATCH (&grpc_static_mdstr_table[60])
/* "if-range" */
#define GRPC_MDSTR_IF_RANGE (&grpc_static_mdstr_table[61])
/* "if-unmodified-since" */
#define GRPC_MDSTR_IF_UNMODIFIED_SINCE (&grpc_static_mdstr_table[62])
/* "last-modified" */
#define GRPC_MDSTR_LAST_MODIFIED (&grpc_static_mdstr_table[63])
/* "lb-cost-bin" */
#define GRPC_MDSTR_LB_COST_BIN (&grpc_static_mdstr_table[64])
/* "lb-token" */
#define GRPC_MDSTR_LB_TOKEN (&grpc_static_mdstr_table[65])
/* "link" */
#define GRPC_MDSTR_LINK (&grpc_static_mdstr_table[66])
/* "location" */
#define GRPC_MDSTR_LOCATION (&grpc_static_mdstr_table[67])
/* "max-forwards" */
#define GRPC_MDSTR_MAX_FORWARDS (&grpc_static_mdstr_table[68])
/* ":method" */
#define GRPC_MDSTR_METHOD (&grpc_static_mdstr_table[69])
/* ":path" */
#define GRPC_MDSTR_PATH (&grpc_static_mdstr_table[70])
/* "POST" */
#define GRPC_MDSTR_POST (&grpc_static_mdstr_table[71])
/* "proxy-authenticate" */
#define GRPC_MDSTR_PROXY_AUTHENTICATE (&grpc_static_mdstr_table[72])
/* "proxy-authorization" */
#define GRPC_MDSTR_PROXY_AUTHORIZATION (&grpc_static_mdstr_table[73])
/* "PUT" */
#define GRPC_MDSTR_PUT (&grpc_static_mdstr_table[74])
/* "range" */
#define GRPC_MDSTR_RANGE (&grpc_static_mdstr_table[75])
/* "referer" */
#define GRPC_MDSTR_REFERER (&grpc_static_mdstr_table[76])
/* "refresh" */
#define GRPC_MDSTR_REFRESH (&grpc_static_mdstr_table[77])
/* "retry-after" */
#define GRPC_MDSTR_RETRY_AFTER (&grpc_static_mdstr_table[78])
/* ":scheme" */
#define GRPC_MDSTR_SCHEME (&grpc_static_mdstr_table[79])
/* "server" */
#define GRPC_MDSTR_SERVER (&grpc_static_mdstr_table[80])
/* "set-cookie" */
#define GRPC_MDSTR_SET_COOKIE (&grpc_static_mdstr_table[81])
/* "/" */
#define GRPC_MDSTR_SLASH (&grpc_static_mdstr_table[82])
/* "/index.html" */
#define GRPC_MDSTR_SLASH_INDEX_DOT_HTML (&grpc_static_mdstr_table[83])
/* ":status" */
#define GRPC_MDSTR_STATUS (&grpc_static_mdstr_table[84])
/* "strict-transport-security" */
#define GRPC_MDSTR_STRICT_TRANSPORT_SECURITY (&grpc_static_mdstr_table[85])
/* "te" */
#define GRPC_MDSTR_TE (&grpc_static_mdstr_table[86])
/* "trailers" */
#define GRPC_MDSTR_TRAILERS (&grpc_static_mdstr_table[87])
/* "transfer-encoding" */
#define GRPC_MDSTR_TRANSFER_ENCODING (&grpc_static_mdstr_table[88])
/* "user-agent" */
#define GRPC_MDSTR_USER_AGENT (&grpc_static_mdstr_table[89])
/* "vary" */
#define GRPC_MDSTR_VARY (&grpc_static_mdstr_table[90])
/* "via" */
#define GRPC_MDSTR_VIA (&grpc_static_mdstr_table[91])
/* "www-authenticate" */
#define GRPC_MDSTR_WWW_AUTHENTICATE (&grpc_static_mdstr_table[92])

#define GRPC_STATIC_MDELEM_COUNT 81
extern grpc_mdelem grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
extern uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT];
/* "accept-charset": "" */
#define GRPC_MDELEM_ACCEPT_CHARSET_EMPTY (&grpc_static_mdelem_table[0])
/* "accept": "" */
#define GRPC_MDELEM_ACCEPT_EMPTY (&grpc_static_mdelem_table[1])
/* "accept-encoding": "" */
#define GRPC_MDELEM_ACCEPT_ENCODING_EMPTY (&grpc_static_mdelem_table[2])
/* "accept-encoding": "gzip, deflate" */
#define GRPC_MDELEM_ACCEPT_ENCODING_GZIP_COMMA_DEFLATE \
  (&grpc_static_mdelem_table[3])
/* "accept-language": "" */
#define GRPC_MDELEM_ACCEPT_LANGUAGE_EMPTY (&grpc_static_mdelem_table[4])
/* "accept-ranges": "" */
#define GRPC_MDELEM_ACCEPT_RANGES_EMPTY (&grpc_static_mdelem_table[5])
/* "access-control-allow-origin": "" */
#define GRPC_MDELEM_ACCESS_CONTROL_ALLOW_ORIGIN_EMPTY \
  (&grpc_static_mdelem_table[6])
/* "age": "" */
#define GRPC_MDELEM_AGE_EMPTY (&grpc_static_mdelem_table[7])
/* "allow": "" */
#define GRPC_MDELEM_ALLOW_EMPTY (&grpc_static_mdelem_table[8])
/* ":authority": "" */
#define GRPC_MDELEM_AUTHORITY_EMPTY (&grpc_static_mdelem_table[9])
/* "authorization": "" */
#define GRPC_MDELEM_AUTHORIZATION_EMPTY (&grpc_static_mdelem_table[10])
/* "cache-control": "" */
#define GRPC_MDELEM_CACHE_CONTROL_EMPTY (&grpc_static_mdelem_table[11])
/* "content-disposition": "" */
#define GRPC_MDELEM_CONTENT_DISPOSITION_EMPTY (&grpc_static_mdelem_table[12])
/* "content-encoding": "" */
#define GRPC_MDELEM_CONTENT_ENCODING_EMPTY (&grpc_static_mdelem_table[13])
/* "content-language": "" */
#define GRPC_MDELEM_CONTENT_LANGUAGE_EMPTY (&grpc_static_mdelem_table[14])
/* "content-length": "" */
#define GRPC_MDELEM_CONTENT_LENGTH_EMPTY (&grpc_static_mdelem_table[15])
/* "content-location": "" */
#define GRPC_MDELEM_CONTENT_LOCATION_EMPTY (&grpc_static_mdelem_table[16])
/* "content-range": "" */
#define GRPC_MDELEM_CONTENT_RANGE_EMPTY (&grpc_static_mdelem_table[17])
/* "content-type": "application/grpc" */
#define GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC \
  (&grpc_static_mdelem_table[18])
/* "content-type": "" */
#define GRPC_MDELEM_CONTENT_TYPE_EMPTY (&grpc_static_mdelem_table[19])
/* "cookie": "" */
#define GRPC_MDELEM_COOKIE_EMPTY (&grpc_static_mdelem_table[20])
/* "date": "" */
#define GRPC_MDELEM_DATE_EMPTY (&grpc_static_mdelem_table[21])
/* "etag": "" */
#define GRPC_MDELEM_ETAG_EMPTY (&grpc_static_mdelem_table[22])
/* "expect": "" */
#define GRPC_MDELEM_EXPECT_EMPTY (&grpc_static_mdelem_table[23])
/* "expires": "" */
#define GRPC_MDELEM_EXPIRES_EMPTY (&grpc_static_mdelem_table[24])
/* "from": "" */
#define GRPC_MDELEM_FROM_EMPTY (&grpc_static_mdelem_table[25])
/* "grpc-accept-encoding": "deflate" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE (&grpc_static_mdelem_table[26])
/* "grpc-accept-encoding": "deflate,gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_DEFLATE_COMMA_GZIP \
  (&grpc_static_mdelem_table[27])
/* "grpc-accept-encoding": "gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_GZIP (&grpc_static_mdelem_table[28])
/* "grpc-accept-encoding": "identity" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY \
  (&grpc_static_mdelem_table[29])
/* "grpc-accept-encoding": "identity,deflate" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE \
  (&grpc_static_mdelem_table[30])
/* "grpc-accept-encoding": "identity,deflate,gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP \
  (&grpc_static_mdelem_table[31])
/* "grpc-accept-encoding": "identity,gzip" */
#define GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_GZIP \
  (&grpc_static_mdelem_table[32])
/* "grpc-encoding": "deflate" */
#define GRPC_MDELEM_GRPC_ENCODING_DEFLATE (&grpc_static_mdelem_table[33])
/* "grpc-encoding": "gzip" */
#define GRPC_MDELEM_GRPC_ENCODING_GZIP (&grpc_static_mdelem_table[34])
/* "grpc-encoding": "identity" */
#define GRPC_MDELEM_GRPC_ENCODING_IDENTITY (&grpc_static_mdelem_table[35])
/* "grpc-status": "0" */
#define GRPC_MDELEM_GRPC_STATUS_0 (&grpc_static_mdelem_table[36])
/* "grpc-status": "1" */
#define GRPC_MDELEM_GRPC_STATUS_1 (&grpc_static_mdelem_table[37])
/* "grpc-status": "2" */
#define GRPC_MDELEM_GRPC_STATUS_2 (&grpc_static_mdelem_table[38])
/* "host": "" */
#define GRPC_MDELEM_HOST_EMPTY (&grpc_static_mdelem_table[39])
/* "if-match": "" */
#define GRPC_MDELEM_IF_MATCH_EMPTY (&grpc_static_mdelem_table[40])
/* "if-modified-since": "" */
#define GRPC_MDELEM_IF_MODIFIED_SINCE_EMPTY (&grpc_static_mdelem_table[41])
/* "if-none-match": "" */
#define GRPC_MDELEM_IF_NONE_MATCH_EMPTY (&grpc_static_mdelem_table[42])
/* "if-range": "" */
#define GRPC_MDELEM_IF_RANGE_EMPTY (&grpc_static_mdelem_table[43])
/* "if-unmodified-since": "" */
#define GRPC_MDELEM_IF_UNMODIFIED_SINCE_EMPTY (&grpc_static_mdelem_table[44])
/* "last-modified": "" */
#define GRPC_MDELEM_LAST_MODIFIED_EMPTY (&grpc_static_mdelem_table[45])
/* "lb-cost-bin": "" */
#define GRPC_MDELEM_LB_COST_BIN_EMPTY (&grpc_static_mdelem_table[46])
/* "lb-token": "" */
#define GRPC_MDELEM_LB_TOKEN_EMPTY (&grpc_static_mdelem_table[47])
/* "link": "" */
#define GRPC_MDELEM_LINK_EMPTY (&grpc_static_mdelem_table[48])
/* "location": "" */
#define GRPC_MDELEM_LOCATION_EMPTY (&grpc_static_mdelem_table[49])
/* "max-forwards": "" */
#define GRPC_MDELEM_MAX_FORWARDS_EMPTY (&grpc_static_mdelem_table[50])
/* ":method": "GET" */
#define GRPC_MDELEM_METHOD_GET (&grpc_static_mdelem_table[51])
/* ":method": "POST" */
#define GRPC_MDELEM_METHOD_POST (&grpc_static_mdelem_table[52])
/* ":method": "PUT" */
#define GRPC_MDELEM_METHOD_PUT (&grpc_static_mdelem_table[53])
/* ":path": "/" */
#define GRPC_MDELEM_PATH_SLASH (&grpc_static_mdelem_table[54])
/* ":path": "/index.html" */
#define GRPC_MDELEM_PATH_SLASH_INDEX_DOT_HTML (&grpc_static_mdelem_table[55])
/* "proxy-authenticate": "" */
#define GRPC_MDELEM_PROXY_AUTHENTICATE_EMPTY (&grpc_static_mdelem_table[56])
/* "proxy-authorization": "" */
#define GRPC_MDELEM_PROXY_AUTHORIZATION_EMPTY (&grpc_static_mdelem_table[57])
/* "range": "" */
#define GRPC_MDELEM_RANGE_EMPTY (&grpc_static_mdelem_table[58])
/* "referer": "" */
#define GRPC_MDELEM_REFERER_EMPTY (&grpc_static_mdelem_table[59])
/* "refresh": "" */
#define GRPC_MDELEM_REFRESH_EMPTY (&grpc_static_mdelem_table[60])
/* "retry-after": "" */
#define GRPC_MDELEM_RETRY_AFTER_EMPTY (&grpc_static_mdelem_table[61])
/* ":scheme": "grpc" */
#define GRPC_MDELEM_SCHEME_GRPC (&grpc_static_mdelem_table[62])
/* ":scheme": "http" */
#define GRPC_MDELEM_SCHEME_HTTP (&grpc_static_mdelem_table[63])
/* ":scheme": "https" */
#define GRPC_MDELEM_SCHEME_HTTPS (&grpc_static_mdelem_table[64])
/* "server": "" */
#define GRPC_MDELEM_SERVER_EMPTY (&grpc_static_mdelem_table[65])
/* "set-cookie": "" */
#define GRPC_MDELEM_SET_COOKIE_EMPTY (&grpc_static_mdelem_table[66])
/* ":status": "200" */
#define GRPC_MDELEM_STATUS_200 (&grpc_static_mdelem_table[67])
/* ":status": "204" */
#define GRPC_MDELEM_STATUS_204 (&grpc_static_mdelem_table[68])
/* ":status": "206" */
#define GRPC_MDELEM_STATUS_206 (&grpc_static_mdelem_table[69])
/* ":status": "304" */
#define GRPC_MDELEM_STATUS_304 (&grpc_static_mdelem_table[70])
/* ":status": "400" */
#define GRPC_MDELEM_STATUS_400 (&grpc_static_mdelem_table[71])
/* ":status": "404" */
#define GRPC_MDELEM_STATUS_404 (&grpc_static_mdelem_table[72])
/* ":status": "500" */
#define GRPC_MDELEM_STATUS_500 (&grpc_static_mdelem_table[73])
/* "strict-transport-security": "" */
#define GRPC_MDELEM_STRICT_TRANSPORT_SECURITY_EMPTY \
  (&grpc_static_mdelem_table[74])
/* "te": "trailers" */
#define GRPC_MDELEM_TE_TRAILERS (&grpc_static_mdelem_table[75])
/* "transfer-encoding": "" */
#define GRPC_MDELEM_TRANSFER_ENCODING_EMPTY (&grpc_static_mdelem_table[76])
/* "user-agent": "" */
#define GRPC_MDELEM_USER_AGENT_EMPTY (&grpc_static_mdelem_table[77])
/* "vary": "" */
#define GRPC_MDELEM_VARY_EMPTY (&grpc_static_mdelem_table[78])
/* "via": "" */
#define GRPC_MDELEM_VIA_EMPTY (&grpc_static_mdelem_table[79])
/* "www-authenticate": "" */
#define GRPC_MDELEM_WWW_AUTHENTICATE_EMPTY (&grpc_static_mdelem_table[80])

extern const uint8_t
    grpc_static_metadata_elem_indices[GRPC_STATIC_MDELEM_COUNT * 2];
extern const char *const grpc_static_metadata_strings[GRPC_STATIC_MDSTR_COUNT];
extern const uint8_t grpc_static_accept_encoding_metadata[8];
#define GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(algs) \
  (&grpc_static_mdelem_table[grpc_static_accept_encoding_metadata[(algs)]])
#endif /* GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H */
