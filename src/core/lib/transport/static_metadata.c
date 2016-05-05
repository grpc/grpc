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

#include "src/core/lib/transport/static_metadata.h"

grpc_mdstr grpc_static_mdstr_table[GRPC_STATIC_MDSTR_COUNT];

grpc_mdelem grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 4, 8, 6, 2, 4, 8, 6, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const uint8_t grpc_static_metadata_elem_indices[GRPC_STATIC_MDELEM_COUNT * 2] =
    {11, 35, 10, 35, 12, 35, 12, 49, 13, 35, 14, 35, 15, 35, 16, 35, 17, 35,
     19, 35, 20, 35, 21, 35, 24, 35, 25, 35, 26, 35, 27, 35, 28, 35, 29, 35,
     30, 18, 30, 35, 31, 35, 32, 35, 36, 35, 37, 35, 38, 35, 39, 35, 42, 33,
     42, 34, 42, 48, 42, 53, 42, 54, 42, 55, 42, 56, 43, 33, 43, 48, 43, 53,
     46, 0,  46, 1,  46, 2,  50, 35, 57, 35, 58, 35, 59, 35, 60, 35, 61, 35,
     62, 35, 63, 35, 64, 35, 65, 35, 66, 35, 67, 40, 67, 69, 67, 72, 68, 80,
     68, 81, 70, 35, 71, 35, 73, 35, 74, 35, 75, 35, 76, 35, 77, 41, 77, 51,
     77, 52, 78, 35, 79, 35, 82, 3,  82, 4,  82, 5,  82, 6,  82, 7,  82, 8,
     82, 9,  83, 35, 84, 85, 86, 35, 87, 35, 88, 35, 89, 35, 90, 35};

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
    "census-bin",
    "census-binary-bin",
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
    "load-reporting",
    "location",
    "max-forwards",
    ":method",
    ":path",
    "POST",
    "proxy-authenticate",
    "proxy-authorization",
    "PUT",
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

const uint8_t grpc_static_accept_encoding_metadata[8] = {0,  29, 26, 30,
                                                         28, 32, 27, 31};
