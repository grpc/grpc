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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_CONNECTIVITY_STATE_H
#define GRPC_INTERNAL_CORE_TRANSPORT_CONNECTIVITY_STATE_H

#include <grpc/grpc.h>
#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/workqueue.h"

typedef struct grpc_connectivity_state_watcher {
  /** we keep watchers in a linked list */
  struct grpc_connectivity_state_watcher *next;
  /** closure to notify on change */
  grpc_iomgr_closure *notify;
  /** the current state as believed by the watcher */
  grpc_connectivity_state *current;
} grpc_connectivity_state_watcher;

typedef struct {
  /** current connectivity state */
  grpc_connectivity_state current_state;
  /** all our watchers */
  grpc_connectivity_state_watcher *watchers;
  /** a name to help debugging */
  char *name;
  /** has this state been changed since the last flush? */
  int changed;
} grpc_connectivity_state_tracker;

typedef struct { grpc_iomgr_closure *cbs; } grpc_connectivity_state_flusher;

extern int grpc_connectivity_state_trace;

void grpc_connectivity_state_init(grpc_connectivity_state_tracker *tracker,
                                  grpc_connectivity_state init_state,
                                  const char *name);
void grpc_connectivity_state_destroy(grpc_connectivity_state_tracker *tracker);

/** Set connectivity state; not thread safe; access must be serialized with an
 * external lock */
void grpc_connectivity_state_set(grpc_connectivity_state_tracker *tracker,
                                 grpc_connectivity_state state,
                                 const char *reason);

/** Begin flushing callbacks; not thread safe; access must be serialized using
 * the same external lock as grpc_connectivity_state_set. Initializes flusher.
 */
void grpc_connectivity_state_begin_flush(
    grpc_connectivity_state_tracker *tracker,
    grpc_connectivity_state_flusher *flusher);

/** Complete flushing updates: must not be called with any locks held */
void grpc_connectivity_state_end_flush(
    grpc_connectivity_state_flusher *flusher);

grpc_connectivity_state grpc_connectivity_state_check(
    grpc_connectivity_state_tracker *tracker);

typedef struct {
  /** 1 if the current state is idle (a hint to begin connecting), 0 otherwise
   */
  int current_state_is_idle;
  /** 1 if the state has already changed: in this case the closure passed to
   * grpc_connectivity_state_notify_on_state_change will not be called */
  int state_already_changed;
} grpc_connectivity_state_notify_on_state_change_result;

/** Return 1 if the channel should start connecting, 0 otherwise */
grpc_connectivity_state_notify_on_state_change_result
grpc_connectivity_state_notify_on_state_change(
    grpc_connectivity_state_tracker *tracker, grpc_connectivity_state *current,
    grpc_iomgr_closure *notify) GRPC_MUST_USE_RESULT;

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_CONNECTIVITY_STATE_H */
