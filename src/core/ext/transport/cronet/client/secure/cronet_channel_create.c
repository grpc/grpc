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

#include <grpc/impl/codegen/port_platform.h>

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/cronet/transport/cronet_transport.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport_impl.h"

// Cronet transport object
typedef struct cronet_transport {
  grpc_transport base;  // must be first element in this structure
  void *engine;
  char *host;
} cronet_transport;

extern grpc_transport_vtable grpc_cronet_vtable;

GRPCAPI grpc_channel *grpc_cronet_secure_channel_create(
    void *engine, const char *target, const grpc_channel_args *args,
    void *reserved) {
  gpr_log(GPR_DEBUG,
          "grpc_create_cronet_transport: stream_engine = %p, target=%s", engine,
          target);

  grpc_transport *ct =
      grpc_create_cronet_transport(engine, target, args, reserved);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  return grpc_channel_create(&exec_ctx, target, args,
                             GRPC_CLIENT_DIRECT_CHANNEL, ct);
}
