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

#ifndef GRPC_CORE_LIB_TRANSPORT_CONNECTIVITY_STATE_H
#define GRPC_CORE_LIB_TRANSPORT_CONNECTIVITY_STATE_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"

typedef struct grpc_connectivity_state_watcher {
  /** we keep watchers in a linked list */
  struct grpc_connectivity_state_watcher* next;
  /** closure to notify on change */
  grpc_closure* notify;
  /** the current state as believed by the watcher */
  grpc_connectivity_state* current;
} grpc_connectivity_state_watcher;

typedef struct {
  /** current grpc_connectivity_state */
  gpr_atm current_state_atm;
  /** all our watchers */
  grpc_connectivity_state_watcher* watchers;
  /** a name to help debugging */
  char* name;
} grpc_connectivity_state_tracker;

extern grpc_core::TraceFlag grpc_connectivity_state_trace;

/** enum --> string conversion */
const char* grpc_connectivity_state_name(grpc_connectivity_state state);

void grpc_connectivity_state_init(grpc_connectivity_state_tracker* tracker,
                                  grpc_connectivity_state init_state,
                                  const char* name);
void grpc_connectivity_state_destroy(grpc_connectivity_state_tracker* tracker);

/** Set connectivity state; not thread safe; access must be serialized with an
 *  external lock */
void grpc_connectivity_state_set(grpc_connectivity_state_tracker* tracker,
                                 grpc_connectivity_state state,
                                 const char* reason);

/** Return true if this connectivity state has watchers.
    Access must be serialized with an external lock. */
bool grpc_connectivity_state_has_watchers(
    grpc_connectivity_state_tracker* tracker);

/** Return the last seen connectivity state. No need to synchronize access. */
grpc_connectivity_state grpc_connectivity_state_check(
    grpc_connectivity_state_tracker* tracker);

/** Return 1 if the channel should start connecting, 0 otherwise.
    If current==NULL cancel notify if it is already queued (success==0 in that
    case).
    Access must be serialized with an external lock. */
bool grpc_connectivity_state_notify_on_state_change(
    grpc_connectivity_state_tracker* tracker, grpc_connectivity_state* current,
    grpc_closure* notify);

#endif /* GRPC_CORE_LIB_TRANSPORT_CONNECTIVITY_STATE_H */
