/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

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
#include "src/core/tsi/transport_security_interface.h"

#ifndef NDEBUG
#include "src/core/lib/security/context/security_context.h"
#endif

void grpc_security_pre_init(void) {
  grpc_register_tracer(&grpc_trace_secure_endpoint);
  grpc_register_tracer(&tsi_tracing_enabled);
#ifndef NDEBUG
  grpc_register_tracer(&grpc_trace_auth_context_refcount);
  grpc_register_tracer(&grpc_trace_security_connector_refcount);
#endif
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
