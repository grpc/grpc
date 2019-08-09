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

#include "src/core/lib/surface/channel.h"

#include <inttypes.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/completion_queue.h"

grpc_connectivity_state grpc_channel_check_connectivity_state(
    grpc_channel* channel, int try_to_connect) {
  /* forward through to the underlying client channel */
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_connectivity_state state;
  GRPC_API_TRACE(
      "grpc_channel_check_connectivity_state(channel=%p, try_to_connect=%d)", 2,
      (channel, try_to_connect));
  if (GPR_LIKELY(client_channel_elem->filter == &grpc_client_channel_filter)) {
    state = grpc_client_channel_check_connectivity_state(client_channel_elem,
                                                         try_to_connect);

    return state;
  }
  gpr_log(GPR_ERROR,
          "grpc_channel_check_connectivity_state called on something that is "
          "not a client channel, but '%s'",
          client_channel_elem->filter->name);

  return GRPC_CHANNEL_SHUTDOWN;
}

typedef enum {
  WAITING,
  READY_TO_CALL_BACK,
  CALLING_BACK_AND_FINISHED,
} callback_phase;

namespace {
struct state_watcher {
  gpr_mu mu;
  callback_phase phase;
  grpc_closure on_complete;
  grpc_closure on_timeout;
  grpc_closure watcher_timer_init;
  grpc_timer alarm;
  grpc_connectivity_state state;
  grpc_completion_queue* cq;
  grpc_cq_completion completion_storage;
  grpc_channel* channel;
  grpc_error* error;
  void* tag;
};
}  // namespace

static void delete_state_watcher(state_watcher* w) {
  grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
      grpc_channel_get_channel_stack(w->channel));
  if (client_channel_elem->filter == &grpc_client_channel_filter) {
    GRPC_CHANNEL_INTERNAL_UNREF(w->channel, "watch_channel_connectivity");
  } else {
    abort();
  }
  gpr_mu_destroy(&w->mu);
  gpr_free(w);
}

static void finished_completion(void* pw, grpc_cq_completion* ignored) {
  bool should_delete = false;
  state_watcher* w = static_cast<state_watcher*>(pw);
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
    delete_state_watcher(w);
  }
}

static void partly_done(state_watcher* w, bool due_to_completion,
                        grpc_error* error) {
  bool end_op = false;
  void* end_op_tag = nullptr;
  grpc_error* end_op_error = nullptr;
  grpc_completion_queue* end_op_cq = nullptr;
  grpc_cq_completion* end_op_completion_storage = nullptr;

  if (due_to_completion) {
    grpc_timer_cancel(&w->alarm);
  } else {
    grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
        grpc_channel_get_channel_stack(w->channel));
    grpc_client_channel_watch_connectivity_state(
        client_channel_elem,
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(w->cq)),
        nullptr, &w->on_complete, nullptr);
  }

  gpr_mu_lock(&w->mu);

  if (due_to_completion) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_operation_failures)) {
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
      end_op = true;
      end_op_cq = w->cq;
      end_op_tag = w->tag;
      end_op_error = w->error;
      end_op_completion_storage = &w->completion_storage;
      break;
    case CALLING_BACK_AND_FINISHED:
      GPR_UNREACHABLE_CODE(return );
      break;
  }
  gpr_mu_unlock(&w->mu);

  if (end_op) {
    grpc_cq_end_op(end_op_cq, end_op_tag, end_op_error, finished_completion, w,
                   end_op_completion_storage);
  }

  GRPC_ERROR_UNREF(error);
}

static void watch_complete(void* pw, grpc_error* error) {
  partly_done(static_cast<state_watcher*>(pw), true, GRPC_ERROR_REF(error));
}

static void timeout_complete(void* pw, grpc_error* error) {
  partly_done(static_cast<state_watcher*>(pw), false, GRPC_ERROR_REF(error));
}

int grpc_channel_num_external_connectivity_watchers(grpc_channel* channel) {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  return grpc_client_channel_num_external_connectivity_watchers(
      client_channel_elem);
}

typedef struct watcher_timer_init_arg {
  state_watcher* w;
  gpr_timespec deadline;
} watcher_timer_init_arg;

static void watcher_timer_init(void* arg, grpc_error* error_ignored) {
  watcher_timer_init_arg* wa = static_cast<watcher_timer_init_arg*>(arg);

  grpc_timer_init(&wa->w->alarm, grpc_timespec_to_millis_round_up(wa->deadline),
                  &wa->w->on_timeout);
  gpr_free(wa);
}

int grpc_channel_support_connectivity_watcher(grpc_channel* channel) {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  return client_channel_elem->filter != &grpc_client_channel_filter ? 0 : 1;
}

void grpc_channel_watch_connectivity_state(
    grpc_channel* channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue* cq, void* tag) {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  state_watcher* w = static_cast<state_watcher*>(gpr_malloc(sizeof(*w)));

  GRPC_API_TRACE(
      "grpc_channel_watch_connectivity_state("
      "channel=%p, last_observed_state=%d, "
      "deadline=gpr_timespec { tv_sec: %" PRId64
      ", tv_nsec: %d, clock_type: %d }, "
      "cq=%p, tag=%p)",
      7,
      (channel, (int)last_observed_state, deadline.tv_sec, deadline.tv_nsec,
       (int)deadline.clock_type, cq, tag));

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
  w->channel = channel;
  w->error = nullptr;

  watcher_timer_init_arg* wa = static_cast<watcher_timer_init_arg*>(
      gpr_malloc(sizeof(watcher_timer_init_arg)));
  wa->w = w;
  wa->deadline = deadline;
  GRPC_CLOSURE_INIT(&w->watcher_timer_init, watcher_timer_init, wa,
                    grpc_schedule_on_exec_ctx);

  if (client_channel_elem->filter == &grpc_client_channel_filter) {
    GRPC_CHANNEL_INTERNAL_REF(channel, "watch_channel_connectivity");
    grpc_client_channel_watch_connectivity_state(
        client_channel_elem,
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq)), &w->state,
        &w->on_complete, &w->watcher_timer_init);
  } else {
    abort();
  }
}
