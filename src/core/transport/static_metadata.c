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
gpr_uintptr grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 3, 7, 5, 2, 4, 8, 6, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const gpr_uint8
    grpc_static_metadata_elem_indices[GRPC_STATIC_MDELEM_COUNT * 2] = {
        11, 34, 10, 34, 12, 34, 12, 48, 13, 34, 14, 34, 15, 34, 16, 34, 17, 34,
        19, 34, 20, 34, 21, 34, 23, 34, 24, 34, 25, 34, 26, 34, 27, 34, 28, 34,
        29, 18, 29, 34, 30, 34, 31, 34, 35, 34, 36, 34, 37, 34, 38, 34, 41, 32,
        41, 33, 41, 47, 41, 52, 41, 53, 41, 54, 41, 55, 42, 32, 42, 47, 42, 52,
        45, 0,  45, 1,  45, 2,  49, 34, 56, 34, 57, 34, 58, 34, 59, 34, 60, 34,
        61, 34, 62, 34, 63, 34, 64, 34, 65, 39, 65, 67, 66, 77, 66, 78, 68, 34,
        69, 34, 70, 34, 71, 34, 72, 34, 73, 34, 74, 40, 74, 50, 74, 51, 75, 34,
        76, 34, 79, 3,  79, 4,  79, 5,  79, 6,  79, 7,  79, 8,  79, 9,  80, 34,
        81, 82, 83, 34, 84, 34, 85, 34, 86, 34, 87, 34};

const char *const grpc_static_metadata_strings[GRPC_STATIC_MDSTR_COUNT] = {
    "0",
    "1",
    "2",
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
    "application/grpc",
    ":authority",
    "authorization",
    "cache-control",
    ":census",
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
    "deflate,gzip",
    "",
    "etag",
    "expect",
    "expires",
    "from",
    "GET",
    "grpc",
    "grpc-accept-encoding",
    "grpc-encoding",
    "grpc-internal-encoding-request",
    "grpc-message",
    "grpc-status",
    "grpc-timeout",
    "gzip",
    "gzip, deflate",
    "host",
    "http",
    "https",
    "identity",
    "identity,deflate",
    "identity,deflate,gzip",
    "identity,gzip",
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

const gpr_uint8 grpc_static_accept_encoding_metadata[8] = {0,  29, 26, 30,
                                                           28, 32, 27, 31};
