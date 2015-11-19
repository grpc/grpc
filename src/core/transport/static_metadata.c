/*
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 * * Neither the name of Google Inc. nor the names of its
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
 * tools/codegen/core/gen_static_metadata.py,
 * and then re-run it.
 *
 * See metadata.h for an explanation of the interface here, and metadata.c for
 * an
 * explanation of what's going on.
 */

#include "src/core/transport/static_metadata.h"

grpc_mdstr grpc_static_mdstr_table[GRPC_STATIC_MDSTR_COUNT];

grpc_mdelem grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];

const gpr_uint8 grpc_static_metadata_elem_indices[GRPC_STATIC_MDELEM_COUNT *
                                                  2] = {
    9,  29, 8,  29, 10, 29, 10, 42, 11, 29, 12, 29, 13, 29, 14, 29, 15, 29, 16,
    29, 17, 29, 18, 29, 19, 29, 20, 29, 21, 29, 22, 29, 23, 29, 24, 29, 25, 29,
    26, 29, 27, 29, 30, 29, 31, 29, 32, 29, 33, 29, 39, 0,  43, 29, 47, 29, 48,
    29, 49, 29, 50, 29, 51, 29, 52, 29, 53, 29, 54, 29, 55, 29, 56, 34, 56, 58,
    57, 68, 57, 69, 59, 29, 60, 29, 61, 29, 62, 29, 63, 29, 64, 29, 65, 35, 65,
    44, 65, 45, 66, 29, 67, 29, 70, 1,  70, 2,  70, 3,  70, 4,  70, 5,  70, 6,
    70, 7,  71, 29, 72, 73, 74, 29, 75, 29, 76, 29, 77, 29, 78, 29};

const char *const grpc_static_metadata_strings[GRPC_STATIC_MDSTR_COUNT] = {
    "0",
    "200",
    "204",
    "206",
    "304",
    "400",
    "404",
    "500",
    "accept",
    "accept-charset",
    "accept-encoding",
    "accept-language",
    "accept-ranges",
    "access-control-allow-origin",
    "age",
    "allow",
    ":authority",
    "authorization",
    "cache-control",
    "content-disposition",
    "content-encoding",
    "content-language",
    "content-length",
    "content-location",
    "content-range",
    "content-type",
    "cookie",
    "date",
    "deflate",
    "",
    "etag",
    "expect",
    "expires",
    "from",
    "GET",
    "grpc",
    "grpc-accept-encoding",
    "grpc-encoding",
    "grpc-message",
    "grpc-status",
    "grpc-timeout",
    "gzip",
    "gzip, deflate",
    "host",
    "http",
    "https",
    "identity",
    "if-match",
    "if-modified-since",
    "if-none-match",
    "if-range",
    "if-unmodified-since",
    "last-modified",
    "link",
    "location",
    "max-forwards",
    ":method",
    ":path",
    "POST",
    "proxy-authenticate",
    "proxy-authorization",
    "range",
    "referer",
    "refresh",
    "retry-after",
    ":scheme",
    "server",
    "set-cookie",
    "/",
    "/index.html",
    ":status",
    "strict-transport-security",
    "te",
    "trailers",
    "transfer-encoding",
    "user-agent",
    "vary",
    "via",
    "www-authenticate"};
