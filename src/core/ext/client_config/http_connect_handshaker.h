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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_HTTP_CONNECT_HANDSHAKER_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_HTTP_CONNECT_HANDSHAKER_H

#include <stdbool.h>

#include "src/core/lib/channel/handshaker.h"

/// Does NOT take ownership of \a server_name or \a authority. These two
/// arguments will be the same most of the time except for load balanced
/// connections to backends where the \a server_name is the IP address of the
/// backend and \authority, the default authority of the channel.
grpc_handshaker* grpc_http_connect_handshaker_create(const char *server_name,
                                                     const char *authority);

/// Returns true if an http proxy is configured. If \a proxy_server is not NULL,
/// it will be set with the configured proxy server (if any) which the caller
/// takes ownership of.
bool grpc_is_http_proxy_configured(char **proxy_server);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_HTTP_CONNECT_HANDSHAKER_H */
