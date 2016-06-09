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

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>

#include "src/core/ext/load_reporting/load_reporting.h"
#include "src/core/ext/load_reporting/load_reporting_filter.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/surface/channel_init.h"

struct grpc_load_reporting_config {
  grpc_load_reporting_fn fn;
  void *user_data;
};

grpc_load_reporting_config *grpc_load_reporting_config_create(
    grpc_load_reporting_fn fn, void *user_data) {
  GPR_ASSERT(fn != NULL);
  grpc_load_reporting_config *lrc =
      gpr_malloc(sizeof(grpc_load_reporting_config));
  lrc->fn = fn;
  lrc->user_data = user_data;
  return lrc;
}

grpc_load_reporting_config *grpc_load_reporting_config_copy(
    grpc_load_reporting_config *src) {
  return grpc_load_reporting_config_create(src->fn, src->user_data);
}

void grpc_load_reporting_config_destroy(grpc_load_reporting_config *lrc) {
  gpr_free(lrc);
}

void grpc_load_reporting_config_call(
    grpc_load_reporting_config *lrc,
    const grpc_load_reporting_call_data *call_data) {
  lrc->fn(call_data, lrc->user_data);
}

static bool is_load_reporting_enabled(const grpc_channel_args *a) {
  if (a == NULL) return false;
  for (size_t i = 0; i < a->num_args; i++) {
    if (0 == strcmp(a->args[i].key, GRPC_ARG_ENABLE_LOAD_REPORTING)) {
      return a->args[i].value.pointer.p != NULL;
    }
  }
  return false;
}

static bool maybe_add_load_reporting_filter(grpc_channel_stack_builder *builder,
                                            void *arg) {
  const grpc_channel_args *args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  if (is_load_reporting_enabled(args)) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, (const grpc_channel_filter *)arg, NULL, NULL);
  }
  return true;
}

static void lrd_arg_destroy(void *p) { grpc_load_reporting_config_destroy(p); }

static void *lrd_arg_copy(void *p) {
  return grpc_load_reporting_config_copy(p);
}

static int lrd_arg_cmp(void *a, void *b) {
  grpc_load_reporting_config *lhs = a;
  grpc_load_reporting_config *rhs = b;
  return !(lhs->fn == rhs->fn && lhs->user_data == rhs->user_data);
}

static const grpc_arg_pointer_vtable lrd_ptr_vtable = {
    lrd_arg_copy, lrd_arg_destroy, lrd_arg_cmp};

grpc_arg grpc_load_reporting_config_create_arg(
    grpc_load_reporting_config *lrc) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_ENABLE_LOAD_REPORTING;
  arg.value.pointer.p = lrc;
  arg.value.pointer.vtable = &lrd_ptr_vtable;
  return arg;
}

/* Plugin registration */

void grpc_load_reporting_plugin_init(void) {
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL, INT_MAX,
                                   maybe_add_load_reporting_filter,
                                   (void *)&grpc_load_reporting_filter);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX,
                                   maybe_add_load_reporting_filter,
                                   (void *)&grpc_load_reporting_filter);
}

void grpc_load_reporting_plugin_shutdown() {}
