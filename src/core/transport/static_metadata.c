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
        11, 33, 10, 33, 12, 33, 12, 47, 13, 33, 14, 33, 15, 33, 16, 33, 17, 33,
        19, 33, 20, 33, 21, 33, 22, 33, 23, 33, 24, 33, 25, 33, 26, 33, 27, 33,
        28, 18, 28, 33, 29, 33, 30, 33, 34, 33, 35, 33, 36, 33, 37, 33, 40, 31,
        40, 32, 40, 46, 40, 51, 40, 52, 40, 53, 40, 54, 41, 31, 41, 46, 41, 51,
        44, 0,  44, 1,  44, 2,  48, 33, 55, 33, 56, 33, 57, 33, 58, 33, 59, 33,
        60, 33, 61, 33, 62, 33, 63, 33, 64, 38, 64, 66, 65, 76, 65, 77, 67, 33,
        68, 33, 69, 33, 70, 33, 71, 33, 72, 33, 73, 39, 73, 49, 73, 50, 74, 33,
        75, 33, 78, 3,  78, 4,  78, 5,  78, 6,  78, 7,  78, 8,  78, 9,  79, 33,
        80, 81, 82, 33, 83, 33, 84, 33, 85, 33, 86, 33};

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
