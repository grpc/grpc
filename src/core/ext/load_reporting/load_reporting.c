/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <limits.h>
#include <string.h>

#include <grpc/load_reporting.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>

#include "src/core/ext/load_reporting/load_reporting.h"
#include "src/core/ext/load_reporting/load_reporting_filter.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_init.h"

static void destroy_lr_cost_context(void *c) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_load_reporting_cost_context *cost_ctx = c;
  for (size_t i = 0; i < cost_ctx->values_count; ++i) {
    grpc_slice_unref_internal(&exec_ctx, cost_ctx->values[i]);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(cost_ctx->values);
  gpr_free(cost_ctx);
}

void grpc_call_set_load_reporting_cost_context(
    grpc_call *call, grpc_load_reporting_cost_context *ctx) {
  grpc_call_context_set(call, GRPC_CONTEXT_LR_COST, ctx,
                        destroy_lr_cost_context);
}

static bool is_load_reporting_enabled(const grpc_channel_args *a) {
  if (a == NULL) return false;
  for (size_t i = 0; i < a->num_args; i++) {
    if (0 == strcmp(a->args[i].key, GRPC_ARG_ENABLE_LOAD_REPORTING)) {
      return a->args[i].type == GRPC_ARG_INTEGER &&
             a->args[i].value.integer != 0;
    }
  }
  return false;
}

static bool maybe_add_load_reporting_filter(grpc_exec_ctx *exec_ctx,
                                            grpc_channel_stack_builder *builder,
                                            void *arg) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (is_load_reporting_enabled(args)) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, (const grpc_channel_filter *)arg, NULL, NULL);
  }
  return true;
}

grpc_arg grpc_load_reporting_enable_arg() {
  grpc_arg arg;
  arg.type = GRPC_ARG_INTEGER;
  arg.key = GRPC_ARG_ENABLE_LOAD_REPORTING;
  arg.value.integer = 1;
  return arg;
}

/* Plugin registration */

void grpc_load_reporting_plugin_init(void) {
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX,
                                   maybe_add_load_reporting_filter,
                                   (void *)&grpc_load_reporting_filter);
}

void grpc_load_reporting_plugin_shutdown() {}
