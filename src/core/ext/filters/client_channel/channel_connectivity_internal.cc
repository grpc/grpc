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

#include "src/core/ext/filters/client_channel/channel_connectivity_internal.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/completion_queue.h"

typedef enum {
  WAITING,
  READY_TO_CALL_BACK,
  CALLING_BACK_AND_FINISHED,
} callback_phase;

typedef struct {
  gpr_mu mu;
  callback_phase phase;
  grpc_closure on_complete;
  grpc_closure on_timeout;
  grpc_closure watcher_timer_init;
  grpc_timer alarm;
  grpc_connectivity_state state;
  grpc_completion_queue *cq;
  grpc_cq_completion completion_storage;
  grpc_channel_element *client_channel_elem;
  grpc_channel_stack *channel_stack;
  grpc_error *error;
  void *tag;
} state_watcher;

static void delete_state_watcher(grpc_exec_ctx *exec_ctx, state_watcher *w) {
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->channel_stack,
                           "watch_channel_connectivity");
  gpr_mu_destroy(&w->mu);
  gpr_free(w);
}

static void finished_completion(grpc_exec_ctx *exec_ctx, void *pw,
                                grpc_cq_completion *ignored) {
  bool should_delete = false;
  state_watcher *w = (state_watcher *)pw;
  gpr_mu_lock(&w->mu);
  switch (w->phase) {
    case WAITING:
    case READY_TO_CALL_BACK:
      GPR_UNREACHABLE_CODE(return );
    case CALLING_BACK_AND_FINISHED:
      should_delete = true;
      break;
  }
  gpr_mu_unlock(&w->mu);

  if (should_delete) {
    delete_state_watcher(exec_ctx, w);
  }
}

static void partly_done(grpc_exec_ctx *exec_ctx, state_watcher *w,
                        bool due_to_completion, grpc_error *error) {
  if (due_to_completion) {
    grpc_timer_cancel(exec_ctx, &w->alarm);
  } else {
    grpc_channel_element *client_channel_elem = w->client_channel_elem;
    grpc_client_channel_watch_connectivity_state(
        exec_ctx, client_channel_elem,
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(w->cq)), NULL,
        &w->on_complete, NULL);
  }

  gpr_mu_lock(&w->mu);

  if (due_to_completion) {
    if (GRPC_TRACER_ON(grpc_trace_operation_failures)) {
      GRPC_LOG_IF_ERROR("watch_completion_error", GRPC_ERROR_REF(error));
    }
    GRPC_ERROR_UNREF(error);
    error = GRPC_ERROR_NONE;
  } else {
    if (error == GRPC_ERROR_NONE) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Timed out waiting for connection state change");
    } else if (error == GRPC_ERROR_CANCELLED) {
      error = GRPC_ERROR_NONE;
    }
  }
  switch (w->phase) {
    case WAITING:
      GRPC_ERROR_REF(error);
      w->error = error;
      w->phase = READY_TO_CALL_BACK;
      break;
    case READY_TO_CALL_BACK:
      if (error != GRPC_ERROR_NONE) {
        GPR_ASSERT(!due_to_completion);
        GRPC_ERROR_UNREF(w->error);
        GRPC_ERROR_REF(error);
        w->error = error;
      }
      w->phase = CALLING_BACK_AND_FINISHED;
      grpc_cq_end_op(exec_ctx, w->cq, w->tag, w->error, finished_completion, w,
                     &w->completion_storage);
      break;
    case CALLING_BACK_AND_FINISHED:
      GPR_UNREACHABLE_CODE(return );
      break;
  }
  gpr_mu_unlock(&w->mu);

  GRPC_ERROR_UNREF(error);
}

static void watch_complete(grpc_exec_ctx *exec_ctx, void *pw,
                           grpc_error *error) {
  partly_done(exec_ctx, (state_watcher *)pw, true, GRPC_ERROR_REF(error));
}

static void timeout_complete(grpc_exec_ctx *exec_ctx, void *pw,
                             grpc_error *error) {
  partly_done(exec_ctx, (state_watcher *)pw, false, GRPC_ERROR_REF(error));
}

typedef struct watcher_timer_init_arg {
  state_watcher *w;
  gpr_timespec deadline;
} watcher_timer_init_arg;

static void watcher_timer_init(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error_ignored) {
  watcher_timer_init_arg *wa = (watcher_timer_init_arg *)arg;

  grpc_timer_init(exec_ctx, &wa->w->alarm,
                  gpr_convert_clock_type(wa->deadline, GPR_CLOCK_MONOTONIC),
                  &wa->w->on_timeout, gpr_now(GPR_CLOCK_MONOTONIC));
  gpr_free(wa);
}

void grpc_channel_watch_connectivity_state_internal(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *client_channel_elem,
    grpc_channel_stack *channel_stack,
    grpc_connectivity_state last_observed_state, gpr_timespec deadline,
    grpc_completion_queue *cq, void *tag) {
  state_watcher *w = (state_watcher *)gpr_malloc(sizeof(*w));

  GPR_ASSERT(grpc_cq_begin_op(cq, tag));

  gpr_mu_init(&w->mu);
  GRPC_CLOSURE_INIT(&w->on_complete, watch_complete, w,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&w->on_timeout, timeout_complete, w,
                    grpc_schedule_on_exec_ctx);
  w->phase = WAITING;
  w->state = last_observed_state;
  w->cq = cq;
  w->tag = tag;
  w->client_channel_elem = client_channel_elem;
  w->channel_stack = channel_stack;
  w->error = NULL;

  watcher_timer_init_arg *wa =
      (watcher_timer_init_arg *)gpr_malloc(sizeof(watcher_timer_init_arg));
  wa->w = w;
  wa->deadline = deadline;
  GRPC_CLOSURE_INIT(&w->watcher_timer_init, watcher_timer_init, wa,
                    grpc_schedule_on_exec_ctx);

  if (client_channel_elem->filter == &grpc_client_channel_filter) {
    GRPC_CHANNEL_STACK_REF(channel_stack, "watch_channel_connectivity");
    grpc_client_channel_watch_connectivity_state(
        exec_ctx, client_channel_elem,
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq)), &w->state,
        &w->on_complete, &w->watcher_timer_init);
  } else {
    abort();
  }
}
