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

#include "src/core/lib/iomgr/executor.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include "src/core/lib/iomgr/exec_ctx.h"

typedef struct grpc_executor_data {
  int busy;          /**< is the thread currently running? */
  int shutting_down; /**< has \a grpc_shutdown() been invoked? */
  int pending_join;  /**< has the thread finished but not been joined? */
  grpc_closure_list closures; /**< collection of pending work */
  gpr_thd_id tid; /**< thread id of the thread, only valid if \a busy or \a
                     pending_join are true */
  gpr_thd_options options;
  gpr_mu mu;
} grpc_executor;

static grpc_executor g_executor;

void grpc_executor_init() {
  memset(&g_executor, 0, sizeof(grpc_executor));
  gpr_mu_init(&g_executor.mu);
  g_executor.options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&g_executor.options);
}

/* thread body */
static void closure_exec_thread_func(void *ignored) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (1) {
    gpr_mu_lock(&g_executor.mu);
    if (g_executor.shutting_down != 0) {
      gpr_mu_unlock(&g_executor.mu);
      break;
    }
    if (grpc_closure_list_empty(g_executor.closures)) {
      /* no more work, time to die */
      GPR_ASSERT(g_executor.busy == 1);
      g_executor.busy = 0;
      gpr_mu_unlock(&g_executor.mu);
      break;
    } else {
      grpc_exec_ctx_enqueue_list(&exec_ctx, &g_executor.closures, NULL);
    }
    gpr_mu_unlock(&g_executor.mu);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

/* Spawn the thread if new work has arrived a no thread is up */
static void maybe_spawn_locked() {
  if (grpc_closure_list_empty(g_executor.closures) == 1) {
    return;
  }
  if (g_executor.shutting_down == 1) {
    return;
  }

  if (g_executor.busy != 0) {
    /* Thread still working. New work will be picked up by already running
     * thread. Not spawning anything. */
    return;
  } else if (g_executor.pending_join != 0) {
    /* Pickup the remains of the previous incarnations of the thread. */
    gpr_thd_join(g_executor.tid);
    g_executor.pending_join = 0;
  }

  /* All previous instances of the thread should have been joined at this point.
   * Spawn time! */
  g_executor.busy = 1;
  gpr_thd_new(&g_executor.tid, closure_exec_thread_func, NULL,
              &g_executor.options);
  g_executor.pending_join = 1;
}

void grpc_executor_enqueue(grpc_closure *closure, bool success) {
  gpr_mu_lock(&g_executor.mu);
  if (g_executor.shutting_down == 0) {
    grpc_closure_list_add(&g_executor.closures, closure, success);
    maybe_spawn_locked();
  }
  gpr_mu_unlock(&g_executor.mu);
}

void grpc_executor_shutdown() {
  int pending_join;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  gpr_mu_lock(&g_executor.mu);
  pending_join = g_executor.pending_join;
  g_executor.shutting_down = 1;
  gpr_mu_unlock(&g_executor.mu);
  /* we can release the lock at this point despite the access to the closure
   * list below because we aren't accepting new work */

  /* Execute pending callbacks, some may be performing cleanups */
  grpc_exec_ctx_enqueue_list(&exec_ctx, &g_executor.closures, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(grpc_closure_list_empty(g_executor.closures));
  if (pending_join) {
    gpr_thd_join(g_executor.tid);
  }
  gpr_mu_destroy(&g_executor.mu);
}
