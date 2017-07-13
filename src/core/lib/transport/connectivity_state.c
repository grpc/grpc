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

#include "src/core/lib/transport/connectivity_state.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

grpc_tracer_flag grpc_connectivity_state_trace =
    GRPC_TRACER_INITIALIZER(false, "connectivity_state");

const char *grpc_connectivity_state_name(grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_INIT:
      return "INIT";
    case GRPC_CHANNEL_IDLE:
      return "IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "CONNECTING";
    case GRPC_CHANNEL_READY:
      return "READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

void grpc_connectivity_state_init(grpc_connectivity_state_tracker *tracker,
                                  grpc_connectivity_state init_state,
                                  const char *name) {
  gpr_atm_no_barrier_store(&tracker->current_state_atm, init_state);
  tracker->current_error = GRPC_ERROR_NONE;
  tracker->watchers = NULL;
  tracker->name = gpr_strdup(name);
}

void grpc_connectivity_state_destroy(grpc_exec_ctx *exec_ctx,
                                     grpc_connectivity_state_tracker *tracker) {
  grpc_error *error;
  grpc_connectivity_state_watcher *w;
  while ((w = tracker->watchers)) {
    tracker->watchers = w->next;

    if (GRPC_CHANNEL_SHUTDOWN != *w->current) {
      *w->current = GRPC_CHANNEL_SHUTDOWN;
      error = GRPC_ERROR_NONE;
    } else {
      error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Shutdown connectivity owner");
    }
    GRPC_CLOSURE_SCHED(exec_ctx, w->notify, error);
    gpr_free(w);
  }
  GRPC_ERROR_UNREF(tracker->current_error);
  gpr_free(tracker->name);
}

grpc_connectivity_state grpc_connectivity_state_check(
    grpc_connectivity_state_tracker *tracker) {
  grpc_connectivity_state cur =
      (grpc_connectivity_state)gpr_atm_no_barrier_load(
          &tracker->current_state_atm);
  if (GRPC_TRACER_ON(grpc_connectivity_state_trace)) {
    gpr_log(GPR_DEBUG, "CONWATCH: %p %s: get %s", tracker, tracker->name,
            grpc_connectivity_state_name(cur));
  }
  return cur;
}

grpc_connectivity_state grpc_connectivity_state_get(
    grpc_connectivity_state_tracker *tracker, grpc_error **error) {
  grpc_connectivity_state cur =
      (grpc_connectivity_state)gpr_atm_no_barrier_load(
          &tracker->current_state_atm);
  if (GRPC_TRACER_ON(grpc_connectivity_state_trace)) {
    gpr_log(GPR_DEBUG, "CONWATCH: %p %s: get %s", tracker, tracker->name,
            grpc_connectivity_state_name(cur));
  }
  if (error != NULL) {
    *error = GRPC_ERROR_REF(tracker->current_error);
  }
  return cur;
}

bool grpc_connectivity_state_has_watchers(
    grpc_connectivity_state_tracker *connectivity_state) {
  return connectivity_state->watchers != NULL;
}

bool grpc_connectivity_state_notify_on_state_change(
    grpc_exec_ctx *exec_ctx, grpc_connectivity_state_tracker *tracker,
    grpc_connectivity_state *current, grpc_closure *notify) {
  grpc_connectivity_state cur =
      (grpc_connectivity_state)gpr_atm_no_barrier_load(
          &tracker->current_state_atm);
  if (GRPC_TRACER_ON(grpc_connectivity_state_trace)) {
    if (current == NULL) {
      gpr_log(GPR_DEBUG, "CONWATCH: %p %s: unsubscribe notify=%p", tracker,
              tracker->name, notify);
    } else {
      gpr_log(GPR_DEBUG, "CONWATCH: %p %s: from %s [cur=%s] notify=%p", tracker,
              tracker->name, grpc_connectivity_state_name(*current),
              grpc_connectivity_state_name(cur), notify);
    }
  }
  if (current == NULL) {
    grpc_connectivity_state_watcher *w = tracker->watchers;
    if (w != NULL && w->notify == notify) {
      GRPC_CLOSURE_SCHED(exec_ctx, notify, GRPC_ERROR_CANCELLED);
      tracker->watchers = w->next;
      gpr_free(w);
      return false;
    }
    while (w != NULL) {
      grpc_connectivity_state_watcher *rm_candidate = w->next;
      if (rm_candidate != NULL && rm_candidate->notify == notify) {
        GRPC_CLOSURE_SCHED(exec_ctx, notify, GRPC_ERROR_CANCELLED);
        w->next = w->next->next;
        gpr_free(rm_candidate);
        return false;
      }
      w = w->next;
    }
    return false;
  } else {
    if (cur != *current) {
      *current = cur;
      GRPC_CLOSURE_SCHED(exec_ctx, notify,
                         GRPC_ERROR_REF(tracker->current_error));
    } else {
      grpc_connectivity_state_watcher *w = gpr_malloc(sizeof(*w));
      w->current = current;
      w->notify = notify;
      w->next = tracker->watchers;
      tracker->watchers = w;
    }
    return cur == GRPC_CHANNEL_IDLE;
  }
}

void grpc_connectivity_state_set(grpc_exec_ctx *exec_ctx,
                                 grpc_connectivity_state_tracker *tracker,
                                 grpc_connectivity_state state,
                                 grpc_error *error, const char *reason) {
  grpc_connectivity_state cur =
      (grpc_connectivity_state)gpr_atm_no_barrier_load(
          &tracker->current_state_atm);
  grpc_connectivity_state_watcher *w;
  if (GRPC_TRACER_ON(grpc_connectivity_state_trace)) {
    const char *error_string = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "SET: %p %s: %s --> %s [%s] error=%p %s", tracker,
            tracker->name, grpc_connectivity_state_name(cur),
            grpc_connectivity_state_name(state), reason, error, error_string);
  }
  switch (state) {
    case GRPC_CHANNEL_INIT:
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_READY:
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      break;
    case GRPC_CHANNEL_SHUTDOWN:
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      GPR_ASSERT(error != GRPC_ERROR_NONE);
      break;
  }
  GRPC_ERROR_UNREF(tracker->current_error);
  tracker->current_error = error;
  if (cur == state) {
    return;
  }
  GPR_ASSERT(cur != GRPC_CHANNEL_SHUTDOWN);
  gpr_atm_no_barrier_store(&tracker->current_state_atm, state);
  while ((w = tracker->watchers) != NULL) {
    *w->current = state;
    tracker->watchers = w->next;
    if (GRPC_TRACER_ON(grpc_connectivity_state_trace)) {
      gpr_log(GPR_DEBUG, "NOTIFY: %p %s: %p", tracker, tracker->name,
              w->notify);
    }
    GRPC_CLOSURE_SCHED(exec_ctx, w->notify,
                       GRPC_ERROR_REF(tracker->current_error));
    gpr_free(w);
  }
}
