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
    grpc_arg arg;
    arg.type = GRPC_ARG_STRING;
    arg.key = GRPC_ARG_DEFAULT_AUTHORITY;
    arg.value.string = default_authority;
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
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL, INT_MAX, append_filter,
                                   (void *)&grpc_client_channel_filter);
  grpc_http_connect_register_handshaker_factory();
}

void grpc_client_channel_shutdown(void) {
  grpc_subchannel_index_shutdown();
  grpc_channel_init_shutdown();
  grpc_proxy_mapper_registry_shutdown();
  grpc_retry_throttle_map_shutdown();
  grpc_resolver_registry_shutdown();
  grpc_lb_policy_registry_shutdown();
}
