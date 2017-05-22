/*
 *
 * Copyright 2017, Google Inc.
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

#include <string.h>

#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/transport_impl.h"

typedef struct {
  const grpc_channel_filter *filter;
  const char *control_channel_arg;
} optional_filter;

static optional_filter compress_filter = {
    &grpc_message_compress_filter, GRPC_ARG_ENABLE_PER_MESSAGE_COMPRESSION};

static bool is_building_http_like_transport(
    grpc_channel_stack_builder *builder) {
  grpc_transport *t = grpc_channel_stack_builder_get_transport(builder);
  return t != NULL && strstr(t->vtable->name, "http");
}

static bool maybe_add_optional_filter(grpc_exec_ctx *exec_ctx,
                                      grpc_channel_stack_builder *builder,
                                      void *arg) {
  if (!is_building_http_like_transport(builder)) return true;
  optional_filter *filtarg = arg;
  const grpc_channel_args *channel_args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  bool enable = grpc_channel_arg_get_bool(
      grpc_channel_args_find(channel_args, filtarg->control_channel_arg),
      !grpc_channel_args_want_minimal_stack(channel_args));
  return enable ? grpc_channel_stack_builder_prepend_filter(
                      builder, filtarg->filter, NULL, NULL)
                : true;
}

static bool maybe_add_required_filter(grpc_exec_ctx *exec_ctx,
                                      grpc_channel_stack_builder *builder,
                                      void *arg) {
  return is_building_http_like_transport(builder)
             ? grpc_channel_stack_builder_prepend_filter(
                   builder, (const grpc_channel_filter *)arg, NULL, NULL)
             : true;
}

void grpc_http_filters_init(void) {
  grpc_register_tracer("compression", &grpc_compression_trace);
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_optional_filter, &compress_filter);
  grpc_channel_init_register_stage(GRPC_CLIENT_DIRECT_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_optional_filter, &compress_filter);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_optional_filter, &compress_filter);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_SUBCHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_required_filter, (void *)&grpc_http_client_filter);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_required_filter, (void *)&grpc_http_client_filter);
  grpc_channel_init_register_stage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_required_filter, (void *)&grpc_http_server_filter);
}

void grpc_http_filters_shutdown(void) {}
