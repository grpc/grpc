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
#include <stdint.h>

#include "absl/base/thread_annotations.h"

#include <grpc/fork.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/forkable.h"
#include "src/core/lib/gprpp/fork.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/authorization/grpc_server_authz_filter.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"

/* (generated) built in registry of plugins */
extern void grpc_register_built_in_plugins(void);

#define MAX_PLUGINS 128

static gpr_once g_basic_init = GPR_ONCE_INIT;
static grpc_core::Mutex* g_init_mu;
static int g_initializations ABSL_GUARDED_BY(g_init_mu) = []() {
  grpc_core::CoreConfiguration::SetDefaultBuilder(
      grpc_core::BuildCoreConfiguration);
  return 0;
}();
static grpc_core::CondVar* g_shutting_down_cv;
static bool g_shutting_down ABSL_GUARDED_BY(g_init_mu) = false;

static bool maybe_prepend_client_auth_filter(
    grpc_core::ChannelStackBuilder* builder) {
  if (builder->channel_args().Contains(GRPC_ARG_SECURITY_CONNECTOR)) {
    builder->PrependFilter(&grpc_core::ClientAuthFilter::kFilter);
  }
  return true;
}

static bool maybe_prepend_server_auth_filter(
    grpc_core::ChannelStackBuilder* builder) {
  if (builder->channel_args().Contains(GRPC_SERVER_CREDENTIALS_ARG)) {
    builder->PrependFilter(&grpc_server_auth_filter);
  }
  return true;
}

static bool maybe_prepend_grpc_server_authz_filter(
    grpc_core::ChannelStackBuilder* builder) {
  if (builder->channel_args().GetPointer<grpc_authorization_policy_provider>(
          GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER) != nullptr) {
    builder->PrependFilter(&grpc_core::GrpcServerAuthzFilter::kFilterVtable);
  }
  return true;
}

namespace grpc_core {
void RegisterSecurityFilters(CoreConfiguration::Builder* builder) {
  // Register the auth client with a priority < INT_MAX to allow the authority
  // filter -on which the auth filter depends- to be higher on the channel
  // stack.
  builder->channel_init()->RegisterStage(GRPC_CLIENT_SUBCHANNEL, INT_MAX - 1,
                                         maybe_prepend_client_auth_filter);
  builder->channel_init()->RegisterStage(GRPC_CLIENT_DIRECT_CHANNEL,
                                         INT_MAX - 1,
                                         maybe_prepend_client_auth_filter);
  builder->channel_init()->RegisterStage(GRPC_SERVER_CHANNEL, INT_MAX - 1,
                                         maybe_prepend_server_auth_filter);
  // Register the GrpcServerAuthzFilter with a priority less than
  // server_auth_filter to allow server_auth_filter on which the grpc filter
  // depends on to be higher on the channel stack.
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, INT_MAX - 2, maybe_prepend_grpc_server_authz_filter);
}
}  // namespace grpc_core

static void do_basic_init(void) {
  gpr_log_verbosity_init();
  g_init_mu = new grpc_core::Mutex();
  g_shutting_down_cv = new grpc_core::CondVar();
  grpc_register_built_in_plugins();
  gpr_time_init();
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
  gpr_once_init(&g_basic_init, do_basic_init);

  grpc_core::MutexLock lock(g_init_mu);
  if (++g_initializations == 1) {
    if (g_shutting_down) {
      g_shutting_down = false;
      g_shutting_down_cv->SignalAll();
    }
    grpc_core::Fork::GlobalInit();
    grpc_event_engine::experimental::RegisterForkHandlers();
    grpc_fork_handlers_auto_register();
    grpc_core::ApplicationCallbackExecCtx::GlobalInit();
    grpc_iomgr_init();
    gpr_timers_global_init();
    for (int i = 0; i < g_number_of_plugins; i++) {
      if (g_all_of_the_plugins[i].init != nullptr) {
        g_all_of_the_plugins[i].init();
      }
    }
    grpc_tracer_init();
    grpc_iomgr_start();
  }

  GRPC_API_TRACE("grpc_init(void)", 0, ());
}

void grpc_shutdown_internal_locked(void)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(g_init_mu) {
  int i;
  {
    grpc_core::ExecCtx exec_ctx(0);
    grpc_iomgr_shutdown_background_closure();
    {
      grpc_timer_manager_set_threading(false);  // shutdown timer_manager thread
      for (i = g_number_of_plugins; i >= 0; i--) {
        if (g_all_of_the_plugins[i].destroy != nullptr) {
          g_all_of_the_plugins[i].destroy();
        }
      }
    }
    grpc_iomgr_shutdown();
    gpr_timers_global_destroy();
    grpc_tracer_shutdown();
    grpc_core::Fork::GlobalShutdown();
  }
  grpc_core::ApplicationCallbackExecCtx::GlobalShutdown();
  g_shutting_down = false;
  g_shutting_down_cv->SignalAll();
}

void grpc_shutdown_internal(void* /*ignored*/) {
  GRPC_API_TRACE("grpc_shutdown_internal", 0, ());
  grpc_core::MutexLock lock(g_init_mu);
  // We have released lock from the shutdown thread and it is possible that
  // another grpc_init has been called, and do nothing if that is the case.
  if (--g_initializations != 0) {
    return;
  }
  grpc_shutdown_internal_locked();
}

void grpc_shutdown(void) {
  GRPC_API_TRACE("grpc_shutdown(void)", 0, ());
  grpc_core::MutexLock lock(g_init_mu);

  if (--g_initializations == 0) {
    grpc_core::ApplicationCallbackExecCtx* acec =
        grpc_core::ApplicationCallbackExecCtx::Get();
    if (!grpc_iomgr_is_any_background_poller_thread() &&
        (acec == nullptr ||
         (acec->Flags() & GRPC_APP_CALLBACK_EXEC_CTX_FLAG_IS_INTERNAL_THREAD) ==
             0)) {
      // just run clean-up when this is called on non-executor thread.
      gpr_log(GPR_DEBUG, "grpc_shutdown starts clean-up now");
      g_shutting_down = true;
      grpc_shutdown_internal_locked();
    } else {
      // spawn a detached thread to do the actual clean up in case we are
      // currently in an executor thread.
      gpr_log(GPR_DEBUG, "grpc_shutdown spawns clean-up thread");
      g_initializations++;
      g_shutting_down = true;
      grpc_core::Thread cleanup_thread(
          "grpc_shutdown", grpc_shutdown_internal, nullptr, nullptr,
          grpc_core::Thread::Options().set_joinable(false).set_tracked(false));
      cleanup_thread.Start();
    }
  }
}

void grpc_shutdown_blocking(void) {
  GRPC_API_TRACE("grpc_shutdown_blocking(void)", 0, ());
  grpc_core::MutexLock lock(g_init_mu);
  if (--g_initializations == 0) {
    g_shutting_down = true;
    grpc_shutdown_internal_locked();
  }
}

int grpc_is_initialized(void) {
  int r;
  gpr_once_init(&g_basic_init, do_basic_init);
  grpc_core::MutexLock lock(g_init_mu);
  r = g_initializations > 0;
  return r;
}

void grpc_maybe_wait_for_async_shutdown(void) {
  gpr_once_init(&g_basic_init, do_basic_init);
  grpc_core::MutexLock lock(g_init_mu);
  while (g_shutting_down) {
    g_shutting_down_cv->Wait(g_init_mu);
  }
}
