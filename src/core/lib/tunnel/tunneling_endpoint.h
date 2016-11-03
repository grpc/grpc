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

#ifndef SRC_CORE_LIB_TUNNEL_GRPC_TUNNEL_H_
#define SRC_CORE_LIB_TUNNEL_GRPC_TUNNEL_H_

#include "src/core/lib/surface/call.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"

/** Create a tunneling endpoint given a call over which the tunnel is
 established.
 The caller retains ownership of the call and metadata pointers. For calls
 where grpc_call_is_client(call) is true, the trailing metadata is populated
 on receipt of GRPC_OP_RECV_STATUS_ON_CLIENT. For calls where
 grpc_call_is_client(call) is false, the contents of trailing metadata is
 sent as soon as the endpoint terminates lameduck by sending
 GRPC_OP_RECV_CLOSE_ON_SERVER */
grpc_error* grpc_tunneling_endpoint_create(grpc_exec_ctx *exec_ctx,
                                           grpc_call *call,
                                           bool is_authoritative,
                                           grpc_closure *notify_on_connect_cb,
                                           grpc_endpoint **endpoint);

/**
 * ep must be a grpc_tunnel endpoint. trailing_metadata only applies for
 * a call implementing a server.
 */
void grpc_tunneling_endpoint_destory(grpc_exec_ctx *exec_ctx,
                                     grpc_endpoint *ep, grpc_closure *done);

#endif /* SRC_CORE_LIB_TUNNEL_GRPC_TUNNEL_H_ */
