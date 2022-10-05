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

#include "src/core/lib/iomgr/iomgr.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/buffer_list.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/internal_errqueue.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/timer_manager.h"

static gpr_mu g_mu;
static gpr_cv g_rcv;
static int g_shutdown;

void grpc_iomgr_init() {
  grpc_core::ExecCtx exec_ctx;
  if (!grpc_have_determined_iomgr_platform()) {
    grpc_set_default_iomgr_platform();
  }
  g_shutdown = 0;
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_rcv);
  grpc_core::Executor::InitAll();
  grpc_iomgr_platform_init();
  grpc_timer_list_init();
}

void grpc_iomgr_start() { grpc_timer_manager_init(); }

void grpc_iomgr_shutdown() {
  grpc_timer_manager_shutdown();
  grpc_iomgr_platform_flush();

  gpr_mu_lock(&g_mu);
  g_shutdown = 1;
  gpr_mu_unlock(&g_mu);
  grpc_timer_list_shutdown();
  grpc_core::ExecCtx::Get()->Flush();
  grpc_core::Executor::ShutdownAll();

  /* ensure all threads have left g_mu */
  gpr_mu_lock(&g_mu);
  gpr_mu_unlock(&g_mu);

  grpc_iomgr_platform_shutdown();
  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_rcv);
}

void grpc_iomgr_shutdown_background_closure() {
  grpc_iomgr_platform_shutdown_background_closure();
}

bool grpc_iomgr_is_any_background_poller_thread() {
  return grpc_iomgr_platform_is_any_background_poller_thread();
}

bool grpc_iomgr_add_closure_to_background_poller(grpc_closure* closure,
                                                 grpc_error_handle error) {
  return grpc_iomgr_platform_add_closure_to_background_poller(closure, error);
}
