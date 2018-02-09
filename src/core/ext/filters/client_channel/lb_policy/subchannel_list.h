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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/transport/connectivity_state.h"

// TODO(roth): This code is intended to be shared between pick_first and
// round_robin.  However, the interface needs more work to provide clean
// encapsulation.  For example, the structs here have some fields that are
// only used in one of the two (e.g., the state counters in
// grpc_lb_subchannel_list and the prev_connectivity_state field in
// grpc_lb_subchannel_data are only used in round_robin, and the
// checking_subchannel field in grpc_lb_subchannel_list is only used by
// pick_first).  Also, there is probably some code duplication between the
// connectivity state notification callback code in both pick_first and
// round_robin that could be refactored and moved here.  In a future PR,
// need to clean this up.

typedef struct grpc_lb_subchannel_list grpc_lb_subchannel_list;

typedef struct {
  /** backpointer to owning subchannel list */
  grpc_lb_subchannel_list* subchannel_list;
  /** subchannel itself */
  grpc_subchannel* subchannel;
  grpc_core::RefCountedPtr<grpc_core::ConnectedSubchannel> connected_subchannel;
  /** Is a connectivity notification pending? */
  bool connectivity_notification_pending;
  /** notification that connectivity has changed on subchannel */
  grpc_closure connectivity_changed_closure;
  /** previous and current connectivity states.  Updated by \a
   * \a connectivity_changed_closure based on
   * \a pending_connectivity_state_unsafe. */
  grpc_connectivity_state prev_connectivity_state;
  grpc_connectivity_state curr_connectivity_state;
  /** connectivity state to be updated by
   * grpc_subchannel_notify_on_state_change(), not guarded by
   * the combiner.  To be copied to \a curr_connectivity_state by
   * \a connectivity_changed_closure. */
  grpc_connectivity_state pending_connectivity_state_unsafe;
  /** the subchannel's target user data */
  void* user_data;
  /** vtable to operate over \a user_data */
  const grpc_lb_user_data_vtable* user_data_vtable;
} grpc_lb_subchannel_data;

/// Unrefs the subchannel contained in sd.
void grpc_lb_subchannel_data_unref_subchannel(grpc_lb_subchannel_data* sd,
                                              const char* reason);

/// Starts watching the connectivity state of the subchannel.
/// The connectivity_changed_cb callback must invoke either
/// grpc_lb_subchannel_data_stop_connectivity_watch() or again call
/// grpc_lb_subchannel_data_start_connectivity_watch().
void grpc_lb_subchannel_data_start_connectivity_watch(
    grpc_lb_subchannel_data* sd);

/// Stops watching the connectivity state of the subchannel.
void grpc_lb_subchannel_data_stop_connectivity_watch(
    grpc_lb_subchannel_data* sd);

struct grpc_lb_subchannel_list {
  /** backpointer to owning policy */
  grpc_core::LoadBalancingPolicy* policy;

  grpc_core::TraceFlag* tracer;

  /** all our subchannels */
  size_t num_subchannels;
  grpc_lb_subchannel_data* subchannels;

  /** Index into subchannels of the one we're currently checking.
   * Used when connecting to subchannels serially instead of in parallel. */
  // TODO(roth): When we have time, we can probably make this go away
  // and compute the index dynamically by subtracting
  // subchannel_list->subchannels from the subchannel_data pointer.
  size_t checking_subchannel;

  /** how many subchannels are in state READY */
  size_t num_ready;
  /** how many subchannels are in state TRANSIENT_FAILURE */
  size_t num_transient_failures;
  /** how many subchannels are in state IDLE */
  size_t num_idle;

  /** There will be one ref for each entry in subchannels for which there is a
   * pending connectivity state watcher callback. */
  gpr_refcount refcount;

  /** Is this list shutting down? This may be true due to the shutdown of the
   * policy itself or because a newer update has arrived while this one hadn't
   * finished processing. */
  bool shutting_down;
};

grpc_lb_subchannel_list* grpc_lb_subchannel_list_create(
    grpc_core::LoadBalancingPolicy* p, grpc_core::TraceFlag* tracer,
    const grpc_lb_addresses* addresses, grpc_combiner* combiner,
    grpc_client_channel_factory* client_channel_factory,
    const grpc_channel_args& args, grpc_iomgr_cb_func connectivity_changed_cb);

void grpc_lb_subchannel_list_ref(grpc_lb_subchannel_list* subchannel_list,
                                 const char* reason);

void grpc_lb_subchannel_list_unref(grpc_lb_subchannel_list* subchannel_list,
                                   const char* reason);

/// Mark subchannel_list as discarded. Unsubscribes all its subchannels. The
/// connectivity state notification callback will ultimately unref it.
void grpc_lb_subchannel_list_shutdown_and_unref(
    grpc_lb_subchannel_list* subchannel_list, const char* reason);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H */
