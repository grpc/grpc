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

#include "src/core/lib/transport/connectivity_state.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

int grpc_connectivity_state_trace = 0;

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
  tracker->current_state = init_state;
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
      error = GRPC_ERROR_CREATE("Shutdown connectivity owner");
    }
    grpc_closure_sched(exec_ctx, w->notify, error);
    gpr_free(w);
  }
  GRPC_ERROR_UNREF(tracker->current_error);
  gpr_free(tracker->name);
}

grpc_connectivity_state grpc_connectivity_state_check(
    grpc_connectivity_state_tracker *tracker, grpc_error **error) {
  if (grpc_connectivity_state_trace) {
    gpr_log(GPR_DEBUG, "CONWATCH: %p %s: get %s", tracker, tracker->name,
            grpc_connectivity_state_name(tracker->current_state));
  }
  if (error != NULL) {
    *error = GRPC_ERROR_REF(tracker->current_error);
  }
  return tracker->current_state;
}

bool grpc_connectivity_state_has_watchers(
    grpc_connectivity_state_tracker *connectivity_state) {
  return connectivity_state->watchers != NULL;
}

bool grpc_connectivity_state_notify_on_state_change(
    grpc_exec_ctx *exec_ctx, grpc_connectivity_state_tracker *tracker,
    grpc_connectivity_state *current, grpc_closure *notify) {
  if (grpc_connectivity_state_trace) {
    if (current == NULL) {
      gpr_log(GPR_DEBUG, "CONWATCH: %p %s: unsubscribe notify=%p", tracker,
              tracker->name, notify);
    } else {
      gpr_log(GPR_DEBUG, "CONWATCH: %p %s: from %s [cur=%s] notify=%p", tracker,
              tracker->name, grpc_connectivity_state_name(*current),
              grpc_connectivity_state_name(tracker->current_state), notify);
    }
  }
  if (current == NULL) {
    grpc_connectivity_state_watcher *w = tracker->watchers;
    if (w != NULL && w->notify == notify) {
      grpc_closure_sched(exec_ctx, notify, GRPC_ERROR_CANCELLED);
      tracker->watchers = w->next;
      gpr_free(w);
      return false;
    }
    while (w != NULL) {
      grpc_connectivity_state_watcher *rm_candidate = w->next;
      if (rm_candidate != NULL && rm_candidate->notify == notify) {
        grpc_closure_sched(exec_ctx, notify, GRPC_ERROR_CANCELLED);
        w->next = w->next->next;
        gpr_free(rm_candidate);
        return false;
      }
      w = w->next;
    }
    return false;
  } else {
    if (tracker->current_state != *current) {
      *current = tracker->current_state;
      grpc_closure_sched(exec_ctx, notify,
                         GRPC_ERROR_REF(tracker->current_error));
    } else {
      grpc_connectivity_state_watcher *w = gpr_malloc(sizeof(*w));
      w->current = current;
      w->notify = notify;
      w->next = tracker->watchers;
      tracker->watchers = w;
    }
    return tracker->current_state == GRPC_CHANNEL_IDLE;
  }
}

void grpc_connectivity_state_set(grpc_exec_ctx *exec_ctx,
                                 grpc_connectivity_state_tracker *tracker,
                                 grpc_connectivity_state state,
                                 grpc_error *error, const char *reason) {
  grpc_connectivity_state_watcher *w;
  if (grpc_connectivity_state_trace) {
    const char *error_string = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "SET: %p %s: %s --> %s [%s] error=%p %s", tracker,
            tracker->name, grpc_connectivity_state_name(tracker->current_state),
            grpc_connectivity_state_name(state), reason, error, error_string);
    grpc_error_free_string(error_string);
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
  if (tracker->current_state == state) {
    return;
  }
  GPR_ASSERT(tracker->current_state != GRPC_CHANNEL_SHUTDOWN);
  tracker->current_state = state;
  while ((w = tracker->watchers) != NULL) {
    *w->current = tracker->current_state;
    tracker->watchers = w->next;
    if (grpc_connectivity_state_trace) {
      gpr_log(GPR_DEBUG, "NOTIFY: %p %s: %p", tracker, tracker->name,
              w->notify);
    }
    grpc_closure_sched(exec_ctx, w->notify,
                       GRPC_ERROR_REF(tracker->current_error));
    gpr_free(w);
  }
}
