/*
 *
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
 *
 */

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_CLIENT_CHTTP2_CONNECTOR_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_CLIENT_CHTTP2_CONNECTOR_H

#include "src/core/ext/client_channel/connector.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/iomgr/exec_ctx.h"

typedef void (*grpc_chttp2_add_handshakers_func)(
    grpc_exec_ctx* exec_ctx, void* user_data,
    grpc_handshake_manager* handshake_mgr);

/// If \a add_handshakers is non-NULL, it will be called with
/// \a add_handshakers_user_data to add handshakers.
grpc_connector* grpc_chttp2_connector_create(
    grpc_exec_ctx* exec_ctx, grpc_chttp2_add_handshakers_func add_handshakers,
    void* add_handshakers_user_data);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_CLIENT_CHTTP2_CONNECTOR_H */
