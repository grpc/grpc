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

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/http_proxy.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/surface/channel_init.h"

static bool append_filter(grpc_exec_ctx *exec_ctx,
                          grpc_channel_stack_builder *builder, void *arg) {
  return grpc_channel_stack_builder_append_filter(
      builder, (const grpc_channel_filter *)arg, NULL, NULL);
}

static bool set_default_host_if_unset(grpc_exec_ctx *exec_ctx,
                                      grpc_channel_stack_builder *builder,
                                      void *unused) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  for (size_t i = 0; i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_DEFAULT_AUTHORITY) ||
        0 == strcmp(args->args[i].key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG)) {
      return true;
    }
  }
  char *default_authority = grpc_get_default_authority(
      exec_ctx, grpc_channel_stack_builder_get_target(builder));
  if (default_authority != NULL) {
    grpc_arg arg = grpc_channel_arg_string_create(GRPC_ARG_DEFAULT_AUTHORITY,
                                                  default_authority);
    grpc_channel_args *new_args = grpc_channel_args_copy_and_add(args, &arg, 1);
    grpc_channel_stack_builder_set_channel_arguments(exec_ctx, builder,
                                                     new_args);
    gpr_free(default_authority);
    grpc_channel_args_destroy(exec_ctx, new_args);
  }
  return true;
}

void grpc_client_channel_init(void) {
  grpc_lb_policy_registry_init();
  grpc_resolver_registry_init();
  grpc_retry_throttle_map_init();
  grpc_proxy_mapper_registry_init();
  grpc_register_http_proxy_mapper();
  grpc_subchannel_index_init();
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL, INT_MIN,
                                   set_default_host_if_unset, NULL);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY, append_filter,
      (void *)&grpc_client_channel_filter);
  grpc_http_connect_register_handshaker_factory();
  grpc_register_tracer(&grpc_client_channel_trace);
#ifndef NDEBUG
  grpc_register_tracer(&grpc_trace_resolver_refcount);
#endif
}

void grpc_client_channel_shutdown(void) {
  grpc_subchannel_index_shutdown();
  grpc_channel_init_shutdown();
  grpc_proxy_mapper_registry_shutdown();
  grpc_retry_throttle_map_shutdown();
  grpc_resolver_registry_shutdown();
  grpc_lb_policy_registry_shutdown();
}
