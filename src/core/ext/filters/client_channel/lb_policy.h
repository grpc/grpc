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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_H

#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/gpr++/ref_counted_ptr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/transport/connectivity_state.h"

/** A load balancing policy: specified by a vtable and a struct (which
    is expected to be extended to contain some parameters) */
typedef struct grpc_lb_policy grpc_lb_policy;
typedef struct grpc_lb_policy_vtable grpc_lb_policy_vtable;
typedef struct grpc_lb_policy_args grpc_lb_policy_args;

extern grpc_core::DebugOnlyTraceFlag grpc_trace_lb_policy_refcount;

struct grpc_lb_policy {
  const grpc_lb_policy_vtable* vtable;
  gpr_refcount refs;
  /* owned pointer to interested parties in load balancing decisions */
  grpc_pollset_set* interested_parties;
  /* combiner under which lb_policy actions take place */
  grpc_combiner* combiner;
  /* callback to force a re-resolution */
  grpc_closure* request_reresolution;
};

/// State used for an LB pick.
typedef struct grpc_lb_policy_pick_state {
  /// Initial metadata associated with the picking call.
  grpc_metadata_batch* initial_metadata;
  /// Bitmask used for selective cancelling. See \a
  /// grpc_lb_policy_cancel_picks() and \a GRPC_INITIAL_METADATA_* in
  /// grpc_types.h.
  uint32_t initial_metadata_flags;
  /// Storage for LB token in \a initial_metadata, or NULL if not used.
  grpc_linked_mdelem lb_token_mdelem_storage;
  /// Closure to run when pick is complete, if not completed synchronously.
  grpc_closure* on_complete;
  /// Will be set to the selected subchannel, or NULL on failure or when
  /// the LB policy decides to drop the call.
  grpc_connected_subchannel* connected_subchannel;
  /// Will be populated with context to pass to the subchannel call, if needed.
  grpc_call_context_element subchannel_call_context[GRPC_CONTEXT_COUNT];
  /// Upon success, \a *user_data will be set to whatever opaque information
  /// may need to be propagated from the LB policy, or NULL if not needed.
  void** user_data;
  /// Next pointer.  For internal use by LB policy.
  struct grpc_lb_policy_pick_state* next;
} grpc_lb_policy_pick_state;

struct grpc_lb_policy_vtable {
  void (*destroy)(grpc_lb_policy* policy);

  /// \see grpc_lb_policy_shutdown_locked().
  void (*shutdown_locked)(grpc_lb_policy* policy, grpc_lb_policy* new_policy);

  /** \see grpc_lb_policy_pick */
  int (*pick_locked)(grpc_lb_policy* policy, grpc_lb_policy_pick_state* pick);

  /** \see grpc_lb_policy_cancel_pick */
  void (*cancel_pick_locked)(grpc_lb_policy* policy,
                             grpc_lb_policy_pick_state* pick,
                             grpc_error* error);

  /** \see grpc_lb_policy_cancel_picks */
  void (*cancel_picks_locked)(grpc_lb_policy* policy,
                              uint32_t initial_metadata_flags_mask,
                              uint32_t initial_metadata_flags_eq,
                              grpc_error* error);

  /** \see grpc_lb_policy_ping_one */
  void (*ping_one_locked)(grpc_lb_policy* policy, grpc_closure* on_initiate,
                          grpc_closure* on_ack);

  /** Try to enter a READY connectivity state */
  void (*exit_idle_locked)(grpc_lb_policy* policy);

  /** check the current connectivity of the lb_policy */
  grpc_connectivity_state (*check_connectivity_locked)(
      grpc_lb_policy* policy, grpc_error** connectivity_error);

  /** call notify when the connectivity state of a channel changes from *state.
      Updates *state with the new state of the policy. Calling with a NULL \a
      state cancels the subscription.  */
  void (*notify_on_state_change_locked)(grpc_lb_policy* policy,
                                        grpc_connectivity_state* state,
                                        grpc_closure* closure);

  void (*update_locked)(grpc_lb_policy* policy,
                        const grpc_lb_policy_args* args);

  /** \see grpc_lb_policy_set_reresolve_closure */
  void (*set_reresolve_closure_locked)(grpc_lb_policy* policy,
                                       grpc_closure* request_reresolution);
};

#ifndef NDEBUG
#define GRPC_LB_POLICY_REF(p, r) \
  grpc_lb_policy_ref((p), __FILE__, __LINE__, (r))
#define GRPC_LB_POLICY_UNREF(p, r) \
  grpc_lb_policy_unref((p), __FILE__, __LINE__, (r))
void grpc_lb_policy_ref(grpc_lb_policy* policy, const char* file, int line,
                        const char* reason);
void grpc_lb_policy_unref(grpc_lb_policy* policy, const char* file, int line,
                          const char* reason);
#else  // !NDEBUG
#define GRPC_LB_POLICY_REF(p, r) grpc_lb_policy_ref((p))
#define GRPC_LB_POLICY_UNREF(p, r) grpc_lb_policy_unref((p))
void grpc_lb_policy_ref(grpc_lb_policy* policy);
void grpc_lb_policy_unref(grpc_lb_policy* policy);
#endif

/** called by concrete implementations to initialize the base struct */
void grpc_lb_policy_init(grpc_lb_policy* policy,
                         const grpc_lb_policy_vtable* vtable,
                         grpc_combiner* combiner);

/// Shuts down \a policy.
/// If \a new_policy is non-null, any pending picks will be restarted
/// on that policy; otherwise, they will be failed.
void grpc_lb_policy_shutdown_locked(grpc_lb_policy* policy,
                                    grpc_lb_policy* new_policy);

/** Finds an appropriate subchannel for a call, based on data in \a pick.
    \a pick must remain alive until the pick is complete.

    If the pick succeeds and a result is known immediately, a non-zero
    value will be returned.  Otherwise, \a pick->on_complete will be invoked
    once the pick is complete with its error argument set to indicate
    success or failure.

    Any IO should be done under the \a interested_parties \a grpc_pollset_set
    in the \a grpc_lb_policy struct. */
int grpc_lb_policy_pick_locked(grpc_lb_policy* policy,
                               grpc_lb_policy_pick_state* pick);

/** Perform a connected subchannel ping (see \a grpc_connected_subchannel_ping)
    against one of the connected subchannels managed by \a policy. */
void grpc_lb_policy_ping_one_locked(grpc_lb_policy* policy,
                                    grpc_closure* on_initiate,
                                    grpc_closure* on_ack);

/** Cancel picks for \a pick.
    The \a on_complete callback of the pending picks will be invoked with \a
    *target set to NULL. */
void grpc_lb_policy_cancel_pick_locked(grpc_lb_policy* policy,
                                       grpc_lb_policy_pick_state* pick,
                                       grpc_error* error);

/** Cancel all pending picks for which their \a initial_metadata_flags (as given
    in the call to \a grpc_lb_policy_pick) matches \a initial_metadata_flags_eq
    when AND'd with \a initial_metadata_flags_mask */
void grpc_lb_policy_cancel_picks_locked(grpc_lb_policy* policy,
                                        uint32_t initial_metadata_flags_mask,
                                        uint32_t initial_metadata_flags_eq,
                                        grpc_error* error);

/** Try to enter a READY connectivity state */
void grpc_lb_policy_exit_idle_locked(grpc_lb_policy* policy);

/* Call notify when the connectivity state of a channel changes from \a *state.
 * Updates \a *state with the new state of the policy */
void grpc_lb_policy_notify_on_state_change_locked(
    grpc_lb_policy* policy, grpc_connectivity_state* state,
    grpc_closure* closure);

grpc_connectivity_state grpc_lb_policy_check_connectivity_locked(
    grpc_lb_policy* policy, grpc_error** connectivity_error);

/** Update \a policy with \a lb_policy_args. */
void grpc_lb_policy_update_locked(grpc_lb_policy* policy,
                                  const grpc_lb_policy_args* lb_policy_args);

/** Set the re-resolution closure to \a request_reresolution. */
void grpc_lb_policy_set_reresolve_closure_locked(
    grpc_lb_policy* policy, grpc_closure* request_reresolution);

/** Try to request a re-resolution. It's NOT a public API; it's only for use by
    the LB policy implementations. */
void grpc_lb_policy_try_reresolve(grpc_lb_policy* policy,
                                  grpc_core::TraceFlag* grpc_lb_trace,
                                  grpc_error* error);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_H */
