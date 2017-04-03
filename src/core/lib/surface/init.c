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
#include <memory.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/compress_filter.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/deadline_filter.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/channel/http_client_filter.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/init.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport_impl.h"

/* (generated) built in registry of plugins */
extern void grpc_register_built_in_plugins(void);

#define MAX_PLUGINS 128

static gpr_once g_basic_init = GPR_ONCE_INIT;
static gpr_mu g_init_mu;
static int g_initializations;

static void do_basic_init(void) {
  gpr_log_verbosity_init();
  gpr_mu_init(&g_init_mu);
  grpc_register_built_in_plugins();
  g_initializations = 0;
}

static bool append_filter(grpc_exec_ctx *exec_ctx,
                          grpc_channel_stack_builder *builder, void *arg) {
  return grpc_channel_stack_builder_append_filter(
      builder, (const grpc_channel_filter *)arg, NULL, NULL);
}

static bool prepend_filter(grpc_exec_ctx *exec_ctx,
                           grpc_channel_stack_builder *builder, void *arg) {
  return grpc_channel_stack_builder_prepend_filter(
      builder, (const grpc_channel_filter *)arg, NULL, NULL);
}

static bool maybe_add_http_filter(grpc_exec_ctx *exec_ctx,
                                  grpc_channel_stack_builder *builder,
                                  void *arg) {
  grpc_transport *t = grpc_channel_stack_builder_get_transport(builder);
  if (t && strstr(t->vtable->name, "http")) {
    return grpc_channel_stack_builder_prepend_filter(
        builder, (const grpc_channel_filter *)arg, NULL, NULL);
  }
  return true;
}

static void register_builtin_channel_init() {
  grpc_channel_init_register_stage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      prepend_filter, (void *)&grpc_client_deadline_filter);
  grpc_channel_init_register_stage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY, prepend_filter,
      (void *)&grpc_server_deadline_filter);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY, prepend_filter,
      (void *)&grpc_compress_filter);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      prepend_filter, (void *)&grpc_compress_filter);
  grpc_channel_init_register_stage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY, prepend_filter,
      (void *)&grpc_compress_filter);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_SUBCHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_http_filter, (void *)&grpc_http_client_filter);
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   grpc_add_connected_filter, NULL);
  grpc_channel_init_register_stage(
      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_http_filter, (void *)&grpc_http_client_filter);
  grpc_channel_init_register_stage(GRPC_CLIENT_DIRECT_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   grpc_add_connected_filter, NULL);
  grpc_channel_init_register_stage(
      GRPC_SERVER_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      maybe_add_http_filter, (void *)&grpc_http_server_filter);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   grpc_add_connected_filter, NULL);
  grpc_channel_init_register_stage(GRPC_CLIENT_LAME_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   append_filter, (void *)&grpc_lame_filter);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX, prepend_filter,
                                   (void *)&grpc_server_top_filter);
}

typedef struct grpc_plugin {
  void (*init)();
  void (*destroy)();
} grpc_plugin;

static grpc_plugin g_all_of_the_plugins[MAX_PLUGINS];
static int g_number_of_plugins = 0;

void grpc_register_plugin(void (*init)(void), void (*destroy)(void)) {
  GRPC_API_TRACE("grpc_register_plugin(init=%p, destroy=%p)", 2,
                 ((void *)(intptr_t)init, (void *)(intptr_t)destroy));
  GPR_ASSERT(g_number_of_plugins != MAX_PLUGINS);
  g_all_of_the_plugins[g_number_of_plugins].init = init;
  g_all_of_the_plugins[g_number_of_plugins].destroy = destroy;
  g_number_of_plugins++;
}

void grpc_init(void) {
  int i;
  gpr_once_init(&g_basic_init, do_basic_init);

  gpr_mu_lock(&g_init_mu);
  if (++g_initializations == 1) {
    gpr_time_init();
    grpc_slice_intern_init();
    grpc_mdctx_global_init();
    grpc_channel_init_init();
    grpc_register_tracer("api", &grpc_api_trace);
    grpc_register_tracer("channel", &grpc_trace_channel);
    grpc_register_tracer("connectivity_state", &grpc_connectivity_state_trace);
    grpc_register_tracer("channel_stack_builder",
                         &grpc_trace_channel_stack_builder);
    grpc_register_tracer("http1", &grpc_http1_trace);
    grpc_register_tracer("compression", &grpc_compression_trace);
    grpc_register_tracer("queue_pluck", &grpc_cq_pluck_trace);
    grpc_register_tracer("combiner", &grpc_combiner_trace);
    grpc_register_tracer("server_channel", &grpc_server_channel_trace);
    grpc_register_tracer("bdp_estimator", &grpc_bdp_estimator_trace);
    // Default pluck trace to 1
    grpc_cq_pluck_trace = 1;
    grpc_register_tracer("queue_timeout", &grpc_cq_event_timeout_trace);
    // Default timeout trace to 1
    grpc_cq_event_timeout_trace = 1;
    grpc_register_tracer("op_failure", &grpc_trace_operation_failures);
    grpc_register_tracer("resource_quota", &grpc_resource_quota_trace);
    grpc_register_tracer("call_error", &grpc_call_error_trace);
#ifndef NDEBUG
    grpc_register_tracer("pending_tags", &grpc_trace_pending_tags);
#endif
    grpc_security_pre_init();
    grpc_iomgr_init();
    grpc_executor_init();
    gpr_timers_global_init();
    grpc_handshaker_factory_registry_init();
    grpc_security_init();
    for (i = 0; i < g_number_of_plugins; i++) {
      if (g_all_of_the_plugins[i].init != NULL) {
        g_all_of_the_plugins[i].init();
      }
    }
    /* register channel finalization AFTER all plugins, to ensure that it's run
     * at the appropriate time */
    grpc_register_security_filters();
    register_builtin_channel_init();
    grpc_tracer_init("GRPC_TRACE");
    /* no more changes to channel init pipelines */
    grpc_channel_init_finalize();
  }
  gpr_mu_unlock(&g_init_mu);
  GRPC_API_TRACE("grpc_init(void)", 0, ());
}

void grpc_shutdown(void) {
  int i;
  GRPC_API_TRACE("grpc_shutdown(void)", 0, ());
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_mu_lock(&g_init_mu);
  if (--g_initializations == 0) {
    grpc_executor_shutdown(&exec_ctx);
    grpc_iomgr_shutdown(&exec_ctx);
    gpr_timers_global_destroy();
    grpc_tracer_shutdown();
    for (i = g_number_of_plugins; i >= 0; i--) {
      if (g_all_of_the_plugins[i].destroy != NULL) {
        g_all_of_the_plugins[i].destroy();
      }
    }
    grpc_mdctx_global_shutdown(&exec_ctx);
    grpc_handshaker_factory_registry_shutdown(&exec_ctx);
    grpc_slice_intern_shutdown();
  }
  gpr_mu_unlock(&g_init_mu);
  grpc_exec_ctx_finish(&exec_ctx);
}

int grpc_is_initialized(void) {
  int r;
  gpr_once_init(&g_basic_init, do_basic_init);
  gpr_mu_lock(&g_init_mu);
  r = g_initializations > 0;
  gpr_mu_unlock(&g_init_mu);
  return r;
}
