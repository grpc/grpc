/*
 *
 * Copyright 2016 gRPC authors.
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
#include <string.h>

#include <grpc/load_reporting.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/load_reporting/server_load_reporting_filter.h"
#include "src/core/ext/filters/load_reporting/server_load_reporting_plugin.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_init.h"

static bool is_load_reporting_enabled(const grpc_channel_args *a) {
  return grpc_channel_arg_get_bool(
      grpc_channel_args_find(a, GRPC_ARG_ENABLE_LOAD_REPORTING), false);
}

static bool maybe_add_server_load_reporting_filter(
    grpc_exec_ctx *exec_ctx, grpc_channel_stack_builder *builder, void *arg) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  const grpc_channel_filter *filter = (const grpc_channel_filter *)arg;
  grpc_channel_stack_builder_iterator *it =
      grpc_channel_stack_builder_iterator_find(builder, filter->name);
  const bool already_has_load_reporting_filter =
      !grpc_channel_stack_builder_iterator_is_end(it);
  grpc_channel_stack_builder_iterator_destroy(it);
  if (is_load_reporting_enabled(args) && !already_has_load_reporting_filter) {
    return grpc_channel_stack_builder_prepend_filter(builder, filter, NULL,
                                                     NULL);
  }
  return true;
}

grpc_arg grpc_load_reporting_enable_arg() {
  return grpc_channel_arg_integer_create((char *)GRPC_ARG_ENABLE_LOAD_REPORTING,
                                         1);
}

/* Plugin registration */

void grpc_server_load_reporting_plugin_init(void) {
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX,
                                   maybe_add_server_load_reporting_filter,
                                   (void *)&grpc_server_load_reporting_filter);
}

void grpc_server_load_reporting_plugin_shutdown() {}
