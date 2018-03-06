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
#include <memory.h>

#include <grpc/fork.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_trace_registry.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/fork.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/iomgr/timer_manager.h"
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
  grpc_fork_support_init();
  gpr_mu_init(&g_init_mu);
  grpc_register_built_in_plugins();
  grpc_cq_global_init();
  g_initializations = 0;
  grpc_fork_handlers_auto_register();
}

static bool append_filter(grpc_channel_stack_builder* builder, void* arg) {
  return grpc_channel_stack_builder_append_filter(
      builder, static_cast<const grpc_channel_filter*>(arg), nullptr, nullptr);
}

static bool prepend_filter(grpc_channel_stack_builder* builder, void* arg) {
  return grpc_channel_stack_builder_prepend_filter(
      builder, static_cast<const grpc_channel_filter*>(arg), nullptr, nullptr);
}

static void register_builtin_channel_init() {
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   grpc_add_connected_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_CLIENT_DIRECT_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   grpc_add_connected_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   grpc_add_connected_filter, nullptr);
  grpc_channel_init_register_stage(GRPC_CLIENT_LAME_CHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   append_filter, (void*)&grpc_lame_filter);
  grpc_channel_init_register_stage(GRPC_SERVER_CHANNEL, INT_MAX, prepend_filter,
                                   (void*)&grpc_server_top_filter);
}

typedef struct grpc_plugin {
  void (*init)();
  void (*destroy)();
} grpc_plugin;

static grpc_plugin g_all_of_the_plugins[MAX_PLUGINS];
static int g_number_of_plugins = 0;

void grpc_register_plugin(void (*init)(void), void (*destroy)(void)) {
  GRPC_API_TRACE("grpc_register_plugin(init=%p, destroy=%p)", 2,
                 ((void*)(intptr_t)init, (void*)(intptr_t)destroy));
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
    grpc_core::Thread::Init();
    grpc_stats_init();
    grpc_slice_intern_init();
    grpc_mdctx_global_init();
    grpc_channel_init_init();
    grpc_channel_trace_registry_init();
    grpc_security_pre_init();
    grpc_core::ExecCtx::GlobalInit();
    grpc_iomgr_init();
    gpr_timers_global_init();
    grpc_handshaker_factory_registry_init();
    grpc_security_init();
    for (i = 0; i < g_number_of_plugins; i++) {
      if (g_all_of_the_plugins[i].init != nullptr) {
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
    grpc_iomgr_start();
  }
  gpr_mu_unlock(&g_init_mu);

  GRPC_API_TRACE("grpc_init(void)", 0, ());
}

void grpc_shutdown(void) {
  int i;
  GRPC_API_TRACE("grpc_shutdown(void)", 0, ());
  gpr_mu_lock(&g_init_mu);
  if (--g_initializations == 0) {
    {
      grpc_core::ExecCtx exec_ctx(0);
      {
        grpc_timer_manager_set_threading(
            false);  // shutdown timer_manager thread
        grpc_executor_shutdown();
        for (i = g_number_of_plugins; i >= 0; i--) {
          if (g_all_of_the_plugins[i].destroy != nullptr) {
            g_all_of_the_plugins[i].destroy();
          }
        }
      }
      grpc_iomgr_shutdown();
      gpr_timers_global_destroy();
      grpc_tracer_shutdown();
      grpc_mdctx_global_shutdown();
      grpc_handshaker_factory_registry_shutdown();
      grpc_slice_intern_shutdown();
      grpc_channel_trace_registry_shutdown();
      grpc_stats_shutdown();
    }
    grpc_core::ExecCtx::GlobalShutdown();
  }
  gpr_mu_unlock(&g_init_mu);
}

int grpc_is_initialized(void) {
  int r;
  gpr_once_init(&g_basic_init, do_basic_init);
  gpr_mu_lock(&g_init_mu);
  r = g_initializations > 0;
  gpr_mu_unlock(&g_init_mu);
  return r;
}
