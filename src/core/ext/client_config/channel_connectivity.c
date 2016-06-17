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

#include "src/core/lib/surface/channel.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/client_config/client_channel.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/completion_queue.h"

grpc_connectivity_state grpc_channel_check_connectivity_state(
    grpc_channel *channel, int try_to_connect) {
  /* forward through to the underlying client channel */
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_connectivity_state state;
  GRPC_API_TRACE(
      "grpc_channel_check_connectivity_state(channel=%p, try_to_connect=%d)", 2,
      (channel, try_to_connect));
  if (client_channel_elem->filter == &grpc_client_channel_filter) {
    state = grpc_client_channel_check_connectivity_state(
        &exec_ctx, client_channel_elem, try_to_connect);
    grpc_exec_ctx_finish(&exec_ctx);
    return state;
  }
  gpr_log(GPR_ERROR,
          "grpc_channel_check_connectivity_state called on something that is "
          "not a (u)client channel, but '%s'",
          client_channel_elem->filter->name);
  grpc_exec_ctx_finish(&exec_ctx);
  return GRPC_CHANNEL_SHUTDOWN;
}

typedef enum {
  WAITING,
  CALLING_BACK,
  CALLING_BACK_AND_FINISHED,
  CALLED_BACK
} callback_phase;

typedef struct {
  gpr_mu mu;
  callback_phase phase;
  int success;
  grpc_closure on_complete;
  grpc_timer alarm;
  grpc_connectivity_state state;
  grpc_completion_queue *cq;
  grpc_cq_completion completion_storage;
  grpc_channel *channel;
  void *tag;
} state_watcher;

static void delete_state_watcher(grpc_exec_ctx *exec_ctx, state_watcher *w) {
  grpc_channel_element *client_channel_elem = grpc_channel_stack_last_element(
      grpc_channel_get_channel_stack(w->channel));
  if (client_channel_elem->filter == &grpc_client_channel_filter) {
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, w->channel,
                                "watch_channel_connectivity");
  } else {
    abort();
  }
  gpr_mu_destroy(&w->mu);
  gpr_free(w);
}

static void finished_completion(grpc_exec_ctx *exec_ctx, void *pw,
                                grpc_cq_completion *ignored) {
  int delete = 0;
  state_watcher *w = pw;
  gpr_mu_lock(&w->mu);
  switch (w->phase) {
    case WAITING:
    case CALLED_BACK:
      GPR_UNREACHABLE_CODE(return );
    case CALLING_BACK:
      w->phase = CALLED_BACK;
      break;
    case CALLING_BACK_AND_FINISHED:
      delete = 1;
      break;
  }
  gpr_mu_unlock(&w->mu);

  if (delete) {
    delete_state_watcher(exec_ctx, w);
  }
}

static void partly_done(grpc_exec_ctx *exec_ctx, state_watcher *w,
                        int due_to_completion) {
  int delete = 0;

  if (due_to_completion) {
    grpc_timer_cancel(exec_ctx, &w->alarm);
  }

  gpr_mu_lock(&w->mu);
  if (due_to_completion) {
    w->success = 1;
  }
  switch (w->phase) {
    case WAITING:
      w->phase = CALLING_BACK;
      grpc_cq_end_op(exec_ctx, w->cq, w->tag, w->success, finished_completion,
                     w, &w->completion_storage);
      break;
    case CALLING_BACK:
      w->phase = CALLING_BACK_AND_FINISHED;
      break;
    case CALLING_BACK_AND_FINISHED:
      GPR_UNREACHABLE_CODE(return );
    case CALLED_BACK:
      delete = 1;
      break;
  }
  gpr_mu_unlock(&w->mu);

  if (delete) {
    delete_state_watcher(exec_ctx, w);
  }
}

static void watch_complete(grpc_exec_ctx *exec_ctx, void *pw, bool success) {
  partly_done(exec_ctx, pw, 1);
}

static void timeout_complete(grpc_exec_ctx *exec_ctx, void *pw, bool success) {
  partly_done(exec_ctx, pw, 0);
}

void grpc_channel_watch_connectivity_state(
    grpc_channel *channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue *cq, void *tag) {
  grpc_channel_element *client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  state_watcher *w = gpr_malloc(sizeof(*w));

  GRPC_API_TRACE(
      "grpc_channel_watch_connectivity_state("
      "channel=%p, last_observed_state=%d, "
      "deadline=gpr_timespec { tv_sec: %lld, tv_nsec: %d, clock_type: %d }, "
      "cq=%p, tag=%p)",
      7, (channel, (int)last_observed_state, (long long)deadline.tv_sec,
          (int)deadline.tv_nsec, (int)deadline.clock_type, cq, tag));

  grpc_cq_begin_op(cq, tag);

  gpr_mu_init(&w->mu);
  grpc_closure_init(&w->on_complete, watch_complete, w);
  w->phase = WAITING;
  w->state = last_observed_state;
  w->success = 0;
  w->cq = cq;
  w->tag = tag;
  w->channel = channel;

  grpc_timer_init(&exec_ctx, &w->alarm,
                  gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC),
                  timeout_complete, w, gpr_now(GPR_CLOCK_MONOTONIC));

  if (client_channel_elem->filter == &grpc_client_channel_filter) {
    GRPC_CHANNEL_INTERNAL_REF(channel, "watch_channel_connectivity");
    grpc_client_channel_watch_connectivity_state(&exec_ctx, client_channel_elem,
                                                 grpc_cq_pollset(cq), &w->state,
                                                 &w->on_complete);
  } else {
    abort();
  }

  grpc_exec_ctx_finish(&exec_ctx);
}
