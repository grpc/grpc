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

#include "src/core/transport/connectivity_state.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

int grpc_connectivity_state_trace = 0;

const char *grpc_connectivity_state_name(grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE: return "IDLE";
    case GRPC_CHANNEL_CONNECTING: return "CONNECTING";
    case GRPC_CHANNEL_READY: return "READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE: return "TRANSIENT_FAILURE";
    case GRPC_CHANNEL_FATAL_FAILURE: return "FATAL_FAILURE";
  }
  abort();
  return "UNKNOWN";
}

void grpc_connectivity_state_init(grpc_connectivity_state_tracker *tracker,
                                  grpc_connectivity_state init_state, const char *name) {
  tracker->current_state = init_state;
  tracker->watchers = NULL;
  tracker->name = gpr_strdup(name);
}

void grpc_connectivity_state_destroy(grpc_connectivity_state_tracker *tracker) {
  grpc_connectivity_state_watcher *w;
  while ((w = tracker->watchers)) {
    tracker->watchers = w->next;

    if (GRPC_CHANNEL_FATAL_FAILURE != *w->current) {
      *w->current = GRPC_CHANNEL_FATAL_FAILURE;
      grpc_iomgr_add_callback(w->notify);
    } else {
      grpc_iomgr_add_delayed_callback(w->notify, 0);
    }
    gpr_free(w);
  }
  gpr_free(tracker->name);
}

grpc_connectivity_state grpc_connectivity_state_check(
    grpc_connectivity_state_tracker *tracker) {
  return tracker->current_state;
}

int grpc_connectivity_state_notify_on_state_change(
    grpc_connectivity_state_tracker *tracker, grpc_connectivity_state *current,
    grpc_iomgr_closure *notify) {
  if (grpc_connectivity_state_trace) {
    gpr_log(GPR_DEBUG, "CONWATCH: %s: from %s [cur=%s]", tracker->name, grpc_connectivity_state_name(*current), grpc_connectivity_state_name(tracker->current_state));
  }
  if (tracker->current_state != *current) {
    *current = tracker->current_state;
    grpc_iomgr_add_callback(notify);
  } else {
    grpc_connectivity_state_watcher *w = gpr_malloc(sizeof(*w));
    w->current = current;
    w->notify = notify;
    w->next = tracker->watchers;
    tracker->watchers = w;
  }
  return tracker->current_state == GRPC_CHANNEL_IDLE;
}

void grpc_connectivity_state_set_with_scheduler(
    grpc_connectivity_state_tracker *tracker, grpc_connectivity_state state,
    void (*scheduler)(void *arg, grpc_iomgr_closure *closure), void *arg) {
  grpc_connectivity_state_watcher *new = NULL;
  grpc_connectivity_state_watcher *w;
  if (grpc_connectivity_state_trace) {
    gpr_log(GPR_DEBUG, "SET: %s: %s --> %s", tracker->name, grpc_connectivity_state_name(tracker->current_state), grpc_connectivity_state_name(state));
  }
  if (tracker->current_state == state) {
    return;
  }
  GPR_ASSERT(tracker->current_state != GRPC_CHANNEL_FATAL_FAILURE);
  tracker->current_state = state;
  while ((w = tracker->watchers)) {
    tracker->watchers = w->next;

    if (state != *w->current) {
      *w->current = state;
      scheduler(arg, w->notify);
      gpr_free(w);
    } else {
      w->next = new;
      new = w;
    }
  }
  tracker->watchers = new;
}

static void default_scheduler(void *ignored, grpc_iomgr_closure *closure) {
  grpc_iomgr_add_callback(closure);
}

void grpc_connectivity_state_set(grpc_connectivity_state_tracker *tracker,
                                 grpc_connectivity_state state) {
  grpc_connectivity_state_set_with_scheduler(tracker, state, default_scheduler,
                                             NULL);
}
