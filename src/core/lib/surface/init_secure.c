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

#include "src/core/lib/surface/init.h"

#include <limits.h>
#include <string.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/tsi/transport_security_interface.h"

void grpc_security_pre_init(void) {
  grpc_register_tracer("secure_endpoint", &grpc_trace_secure_endpoint);
  grpc_register_tracer("transport_security", &tsi_tracing_enabled);
}

static bool maybe_prepend_client_auth_filter(
    grpc_exec_ctx *exec_ctx, grpc_channel_stack_builder *builder, void *arg) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (args) {
    for (size_t i = 0; i < args->num_args; i++) {
      if (0 == strcmp(GRPC_ARG_SECURITY_CONNECTOR, args->args[i].key)) {
        return grpc_channel_stack_builder_prepend_filter(
            builder, &grpc_client_auth_filter, NULL, NULL);
      }
    }
  }
  return true;
}

static bool maybe_prepend_server_auth_filter(
    grpc_exec_ctx *exec_ctx, grpc_channel_stack_builder *builder, void *arg) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (args) {
    for (size_t i = 0; i < args->num_args; i++) {
      if (0 == strcmp(GRPC_SERVER_CREDENTIALS_ARG, args->args[i].key)) {
        return grpc_channel_stack_builder_prepend_filter(
            builder, &grpc_server_auth_filter, NULL, NULL);
      }
    }
  }
  return true;
}

void grpc_register_security_filters(void) {
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL, INT_MAX,
                                   maybe_prepend_client_auth_filter, NULL);
  grpc_channel_init_register_stage(GRPC_CLIENT_DIRECT_CHANNEL, INT_MAX,
                                   maybe_prepend_client_auth_filter, NULL);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX,
                                   maybe_prepend_server_auth_filter, NULL);
}

void grpc_security_init() { grpc_security_register_handshaker_factories(); }
