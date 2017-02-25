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
