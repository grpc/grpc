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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HTTP_CONNECT_HANDSHAKER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HTTP_CONNECT_HANDSHAKER_H

/// Channel arg indicating the server in HTTP CONNECT request (string).
/// The presence of this arg triggers the use of HTTP CONNECT.
#define GRPC_ARG_HTTP_CONNECT_SERVER "grpc.http_connect_server"

/// Channel arg indicating HTTP CONNECT headers (string).
/// Multiple headers are separated by newlines.  Key/value pairs are
/// seperated by colons.
#define GRPC_ARG_HTTP_CONNECT_HEADERS "grpc.http_connect_headers"

/// Registers handshaker factory.
void grpc_http_connect_register_handshaker_factory();

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HTTP_CONNECT_HANDSHAKER_H */
