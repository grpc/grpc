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

GPR_GLOBAL_CONFIG_DEFINE_BOOL(grpc_abort_on_leaks, false,
                              "A debugging aid to cause a call to abort() when "
                              "gRPC objects are leaked past grpc_shutdown()");

static gpr_mu g_mu;
static gpr_cv g_rcv;
static int g_shutdown;
static grpc_iomgr_object g_root_object;
static bool g_grpc_abort_on_leaks;

void grpc_iomgr_init() {
  grpc_core::ExecCtx exec_ctx;
  if (!grpc_have_determined_iomgr_platform()) {
    grpc_set_default_iomgr_platform();
  }
  g_shutdown = 0;
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_rcv);
  grpc_core::Executor::InitAll();
  g_root_object.next = g_root_object.prev = &g_root_object;
  g_root_object.name = const_cast<char*>("root");
  grpc_iomgr_platform_init();
  grpc_timer_list_init();
  g_grpc_abort_on_leaks = GPR_GLOBAL_CONFIG_GET(grpc_abort_on_leaks);
}

void grpc_iomgr_start() { grpc_timer_manager_init(); }

static size_t count_objects(void) {
  grpc_iomgr_object* obj;
  size_t n = 0;
  for (obj = g_root_object.next; obj != &g_root_object; obj = obj->next) {
    n++;
  }
  return n;
}

size_t grpc_iomgr_count_objects_for_testing(void) {
  gpr_mu_lock(&g_mu);
  size_t ret = count_objects();
  gpr_mu_unlock(&g_mu);
  return ret;
}

static void dump_objects(const char* kind) {
  grpc_iomgr_object* obj;
  for (obj = g_root_object.next; obj != &g_root_object; obj = obj->next) {
    gpr_log(GPR_DEBUG, "%s OBJECT: %s %p", kind, obj->name, obj);
  }
}

void grpc_iomgr_shutdown() {
  gpr_timespec shutdown_deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(10, GPR_TIMESPAN));
  gpr_timespec last_warning_time = gpr_now(GPR_CLOCK_REALTIME);

  {
    grpc_timer_manager_shutdown();
    grpc_iomgr_platform_flush();

    gpr_mu_lock(&g_mu);
    g_shutdown = 1;
    while (g_root_object.next != &g_root_object) {
      if (gpr_time_cmp(
              gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), last_warning_time),
              gpr_time_from_seconds(1, GPR_TIMESPAN)) >= 0) {
        if (g_root_object.next != &g_root_object) {
          gpr_log(GPR_DEBUG,
                  "Waiting for %" PRIuPTR " iomgr objects to be destroyed",
                  count_objects());
        }
        last_warning_time = gpr_now(GPR_CLOCK_REALTIME);
      }
      grpc_core::ExecCtx::Get()->SetNowIomgrShutdown();
      if (grpc_timer_check(nullptr) == GRPC_TIMERS_FIRED) {
        gpr_mu_unlock(&g_mu);
        grpc_core::ExecCtx::Get()->Flush();
        grpc_iomgr_platform_flush();
        gpr_mu_lock(&g_mu);
        continue;
      }
      if (g_root_object.next != &g_root_object) {
        if (grpc_iomgr_abort_on_leaks()) {
          gpr_log(GPR_DEBUG,
                  "Failed to free %" PRIuPTR
                  " iomgr objects before shutdown deadline: "
                  "memory leaks are likely",
                  count_objects());
          dump_objects("LEAKED");
          abort();
        }
        gpr_timespec short_deadline =
            gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                         gpr_time_from_millis(100, GPR_TIMESPAN));
        if (gpr_cv_wait(&g_rcv, &g_mu, short_deadline)) {
          if (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), shutdown_deadline) >
              0) {
            if (g_root_object.next != &g_root_object) {
              gpr_log(GPR_DEBUG,
                      "Failed to free %" PRIuPTR
                      " iomgr objects before shutdown deadline: "
                      "memory leaks are likely",
                      count_objects());
              dump_objects("LEAKED");
            }
            break;
          }
        }
      }
    }
    gpr_mu_unlock(&g_mu);
    grpc_timer_list_shutdown();
    grpc_core::ExecCtx::Get()->Flush();
    grpc_core::Executor::ShutdownAll();
  }

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

void grpc_iomgr_register_object(grpc_iomgr_object* obj, const char* name) {
  obj->name = gpr_strdup(name);
  gpr_mu_lock(&g_mu);
  obj->next = &g_root_object;
  obj->prev = g_root_object.prev;
  obj->next->prev = obj->prev->next = obj;
  gpr_mu_unlock(&g_mu);
}

void grpc_iomgr_unregister_object(grpc_iomgr_object* obj) {
  gpr_mu_lock(&g_mu);
  obj->next->prev = obj->prev;
  obj->prev->next = obj->next;
  gpr_cv_signal(&g_rcv);
  gpr_mu_unlock(&g_mu);
  gpr_free(obj->name);
}

bool grpc_iomgr_abort_on_leaks(void) { return g_grpc_abort_on_leaks; }
