/*
 *
 * Copyright 2016, Google Inc.
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
 *
 */

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_HTTP_CONNECT_HANDSHAKER_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_HTTP_CONNECT_HANDSHAKER_H

#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/http/parser.h"

/// Creates a new HTTP CONNECT handshaker.
grpc_handshaker* grpc_http_connect_handshaker_create(const char* proxy_server,
                                                     grpc_http_header* headers,
                                                     size_t num_headers);

/// Returns the name of the proxy to use, or NULL if no proxy is configured.
/// Caller takes ownership of result.
char* grpc_get_http_proxy_server();

/// Registers handshaker factory.
void grpc_http_connect_register_handshaker_factory();

#endif /* GRPC_CORE_EXT_CLIENT_CHANNEL_HTTP_CONNECT_HANDSHAKER_H */
