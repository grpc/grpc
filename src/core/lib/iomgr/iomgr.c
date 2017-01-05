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

#include "src/core/lib/iomgr/iomgr.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/network_status_tracker.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"

static gpr_mu g_mu;
static gpr_cv g_rcv;
static int g_shutdown;
static grpc_iomgr_object g_root_object;

void grpc_iomgr_init(void) {
  g_shutdown = 0;
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_rcv);
  grpc_exec_ctx_global_init();
  grpc_timer_list_init(gpr_now(GPR_CLOCK_MONOTONIC));
  g_root_object.next = g_root_object.prev = &g_root_object;
  g_root_object.name = "root";
  grpc_network_status_init();
  grpc_iomgr_platform_init();
}

static size_t count_objects(void) {
  grpc_iomgr_object *obj;
  size_t n = 0;
  for (obj = g_root_object.next; obj != &g_root_object; obj = obj->next) {
    n++;
  }
  return n;
}

static void dump_objects(const char *kind) {
  grpc_iomgr_object *obj;
  for (obj = g_root_object.next; obj != &g_root_object; obj = obj->next) {
    gpr_log(GPR_DEBUG, "%s OBJECT: %s %p", kind, obj->name, obj);
  }
}

void grpc_iomgr_shutdown(grpc_exec_ctx *exec_ctx) {
  gpr_timespec shutdown_deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(10, GPR_TIMESPAN));
  gpr_timespec last_warning_time = gpr_now(GPR_CLOCK_REALTIME);

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
    if (grpc_timer_check(exec_ctx, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL)) {
      gpr_mu_unlock(&g_mu);
      grpc_exec_ctx_flush(exec_ctx);
      grpc_iomgr_platform_flush();
      gpr_mu_lock(&g_mu);
      continue;
    }
    if (g_root_object.next != &g_root_object) {
      if (grpc_iomgr_abort_on_leaks()) {
        gpr_log(GPR_DEBUG, "Failed to free %" PRIuPTR
                           " iomgr objects before shutdown deadline: "
                           "memory leaks are likely",
                count_objects());
        dump_objects("LEAKED");
        abort();
      }
      gpr_timespec short_deadline = gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(100, GPR_TIMESPAN));
      if (gpr_cv_wait(&g_rcv, &g_mu, short_deadline)) {
        if (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), shutdown_deadline) > 0) {
          if (g_root_object.next != &g_root_object) {
            gpr_log(GPR_DEBUG, "Failed to free %" PRIuPTR
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

  grpc_timer_list_shutdown(exec_ctx);
  grpc_exec_ctx_flush(exec_ctx);

  /* ensure all threads have left g_mu */
  gpr_mu_lock(&g_mu);
  gpr_mu_unlock(&g_mu);

  grpc_iomgr_platform_shutdown();
  grpc_exec_ctx_global_shutdown();
  grpc_network_status_shutdown();
  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_rcv);
}

void grpc_iomgr_register_object(grpc_iomgr_object *obj, const char *name) {
  obj->name = gpr_strdup(name);
  gpr_mu_lock(&g_mu);
  obj->next = &g_root_object;
  obj->prev = g_root_object.prev;
  obj->next->prev = obj->prev->next = obj;
  gpr_mu_unlock(&g_mu);
}

void grpc_iomgr_unregister_object(grpc_iomgr_object *obj) {
  gpr_mu_lock(&g_mu);
  obj->next->prev = obj->prev;
  obj->prev->next = obj->next;
  gpr_cv_signal(&g_rcv);
  gpr_mu_unlock(&g_mu);
  gpr_free(obj->name);
}

bool grpc_iomgr_abort_on_leaks(void) {
  char *env = gpr_getenv("GRPC_ABORT_ON_LEAKS");
  if (env == NULL) return false;
  static const char *truthy[] = {"yes",  "Yes",  "YES", "true",
                                 "True", "TRUE", "1"};
  bool should_we = false;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(truthy); i++) {
    if (0 == strcmp(env, truthy[i])) should_we = true;
  }
  gpr_free(env);
  return should_we;
}
