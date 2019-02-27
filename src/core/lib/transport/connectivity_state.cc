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

#include "src/core/lib/transport/connectivity_state.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

grpc_core::TraceFlag grpc_connectivity_state_trace(false, "connectivity_state");

const char* grpc_connectivity_state_name(grpc_connectivity_state state) {
  switch (state) {
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

void grpc_connectivity_state_init(grpc_connectivity_state_tracker* tracker,
                                  grpc_connectivity_state init_state,
                                  const char* name) {
  gpr_atm_no_barrier_store(&tracker->current_state_atm, init_state);
  tracker->current_error = GRPC_ERROR_NONE;
  tracker->name = gpr_strdup(name);
}

void grpc_connectivity_state_destroy(grpc_connectivity_state_tracker* tracker) {
  grpc_error* error;
  grpc_connectivity_state_watcher* w;
  while ((w = tracker->root_watcher.next) != nullptr) {
    tracker->root_watcher.next = w->next;
    if (GRPC_CHANNEL_SHUTDOWN != *w->current) {
      *w->current = GRPC_CHANNEL_SHUTDOWN;
      error = GRPC_ERROR_NONE;
    } else {
      error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Shutdown connectivity owner");
    }
    GRPC_CLOSURE_SCHED(w->notify, error);
    gpr_free(w);
  }
  GRPC_ERROR_UNREF(tracker->current_error);
  gpr_free(tracker->name);
}

grpc_connectivity_state grpc_connectivity_state_check(
    grpc_connectivity_state_tracker* tracker) {
  grpc_connectivity_state cur = static_cast<grpc_connectivity_state>(
      gpr_atm_no_barrier_load(&tracker->current_state_atm));
  if (grpc_connectivity_state_trace.enabled()) {
    gpr_log(GPR_INFO, "CONWATCH: %p %s: get %s", tracker, tracker->name,
            grpc_connectivity_state_name(cur));
  }
  return cur;
}

grpc_connectivity_state grpc_connectivity_state_get(
    grpc_connectivity_state_tracker* tracker, grpc_error** error) {
  grpc_connectivity_state cur = static_cast<grpc_connectivity_state>(
      gpr_atm_no_barrier_load(&tracker->current_state_atm));
  if (grpc_connectivity_state_trace.enabled()) {
    gpr_log(GPR_INFO, "CONWATCH: %p %s: get %s", tracker, tracker->name,
            grpc_connectivity_state_name(cur));
  }
  if (error != nullptr) {
    *error = GRPC_ERROR_REF(tracker->current_error);
  }
  return cur;
}

bool grpc_connectivity_state_has_watchers(
    grpc_connectivity_state_tracker* connectivity_state) {
  return connectivity_state->root_watcher.next != nullptr;
}

bool grpc_connectivity_state_notify_on_state_change(
    grpc_connectivity_state_tracker* tracker, grpc_connectivity_state* current,
    grpc_closure* notify, bool force_notify_ready) {
  grpc_connectivity_state real_current_state =
      static_cast<grpc_connectivity_state>(
          gpr_atm_no_barrier_load(&tracker->current_state_atm));
  if (grpc_connectivity_state_trace.enabled()) {
    if (current == nullptr) {
      gpr_log(GPR_INFO, "CONWATCH: %p %s: unsubscribe notify=%p", tracker,
              tracker->name, notify);
    } else {
      gpr_log(GPR_INFO, "CONWATCH: %p %s: from %s [cur=%s] notify=%p", tracker,
              tracker->name, grpc_connectivity_state_name(*current),
              grpc_connectivity_state_name(real_current_state), notify);
    }
  }
  if (current == nullptr) {
    grpc_connectivity_state_watcher* prev = &tracker->root_watcher;
    grpc_connectivity_state_watcher* cur = prev->next;
    while (cur != nullptr) {
      if (cur->notify == notify) {
        GRPC_CLOSURE_SCHED(notify, GRPC_ERROR_CANCELLED);
        prev->next = cur->next;
        gpr_free(cur);
        return false;
      }
      prev = cur;
      cur = prev->next;
    }
    return false;
  } else {
    if (real_current_state != *current) {
      *current = real_current_state;
      GRPC_CLOSURE_SCHED(notify, GRPC_ERROR_REF(tracker->current_error));
    } else {
      grpc_connectivity_state_watcher* w =
          static_cast<grpc_connectivity_state_watcher*>(gpr_malloc(sizeof(*w)));
      w->current = current;
      w->notify = notify;
      w->force_notify_ready = force_notify_ready;
      w->next = tracker->root_watcher.next;
      tracker->root_watcher.next = w;
    }
    return real_current_state == GRPC_CHANNEL_IDLE;
  }
}

void grpc_connectivity_state_set(grpc_connectivity_state_tracker* tracker,
                                 grpc_connectivity_state state,
                                 grpc_error* error, const char* reason) {
  grpc_connectivity_state current_state = static_cast<grpc_connectivity_state>(
      gpr_atm_no_barrier_load(&tracker->current_state_atm));
  if (grpc_connectivity_state_trace.enabled()) {
    const char* error_string = grpc_error_string(error);
    gpr_log(GPR_INFO, "SET: %p %s: %s --> %s [%s] error=%p %s", tracker,
            tracker->name, grpc_connectivity_state_name(current_state),
            grpc_connectivity_state_name(state), reason, error, error_string);
  }
  switch (state) {
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_READY:
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      break;
    case GRPC_CHANNEL_SHUTDOWN:
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      GPR_ASSERT(error != GRPC_ERROR_NONE);
  }
  GRPC_ERROR_UNREF(tracker->current_error);
  tracker->current_error = error;
  if (current_state == state && state != GRPC_CHANNEL_READY) return;
  GPR_ASSERT(current_state != GRPC_CHANNEL_SHUTDOWN);
  gpr_atm_no_barrier_store(&tracker->current_state_atm, state);
  grpc_connectivity_state_watcher* prev = &tracker->root_watcher;
  grpc_connectivity_state_watcher* cur = prev->next;
  while (cur != nullptr) {
    if (current_state != state || cur->force_notify_ready) {
      *cur->current = state;
      if (grpc_connectivity_state_trace.enabled()) {
        gpr_log(GPR_INFO, "NOTIFY: %p %s: %p", tracker, tracker->name,
                cur->notify);
      }
      GRPC_CLOSURE_SCHED(cur->notify, GRPC_ERROR_REF(tracker->current_error));
      prev->next = cur->next;
      gpr_free(cur);
    } else {
      prev = cur;
    }
    cur = prev->next;
  }
}
