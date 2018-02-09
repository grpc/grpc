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

#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/transport/connectivity_state.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_lb_policy_refcount;

namespace grpc_core {

/// Interface for load balancing policies.
///
/// Note: All methods with a "Locked" suffix must be called from the
/// combiner passed to the constructor.
///
/// Any I/O done by the LB policy should be done under the pollset_set
/// returned by \a interested_parties().
class LoadBalancingPolicy
    : public InternallyRefCountedWithTracing<LoadBalancingPolicy> {
 public:
  struct Args {
    /// Used to create channels and subchannels.
    grpc_client_channel_factory* client_channel_factory = nullptr;
    /// Channel args from the resolver.
    grpc_channel_args* args = nullptr;
    /// The combiner under which all LB policy calls will be run.
    grpc_combiner* combiner = nullptr;
  };

  /// State used for an LB pick.
  struct PickState {
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
    /// Will be set to the selected subchannel, or nullptr on failure or when
    /// the LB policy decides to drop the call.
    RefCountedPtr<ConnectedSubchannel> connected_subchannel;
    /// Will be populated with context to pass to the subchannel call, if
    /// needed.
    grpc_call_context_element subchannel_call_context[GRPC_CONTEXT_COUNT];
    /// Upon success, \a *user_data will be set to whatever opaque information
    /// may need to be propagated from the LB policy, or NULL if not needed.
    void** user_data;
    /// Next pointer.  For internal use by LB policy.
    PickState* next;
  };

  // Not copyable nor movable.
  LoadBalancingPolicy(const LoadBalancingPolicy&) = delete;
  LoadBalancingPolicy& operator=(const LoadBalancingPolicy&) = delete;

  /// Finds an appropriate subchannel for a call, based on data in \a pick.
  /// \a pick must remain alive until the pick is complete.
  ///
  /// If the pick succeeds and a result is known immediately, returns true.
  /// Otherwise, \a pick->on_complete will be invoked once the pick is
  /// complete with its error argument set to indicate success or failure.
  virtual bool PickLocked(PickState* pick) GRPC_ABSTRACT;

  /// Performs a connected subchannel ping (\see grpc_connected_subchannel_ping)
  /// against one of the connected subchannels managed by the policy.
  virtual void PingOneLocked(grpc_closure* on_initiate,
                             grpc_closure* on_ack) GRPC_ABSTRACT;

  /// Cancels \a pick.
  /// The \a on_complete callback of the pending pick will be invoked with
  /// \a pick->connected_subchannel set to null.
  virtual void CancelPickLocked(PickState* pick,
                                grpc_error* error) GRPC_ABSTRACT;

  /// Cancels all pending picks for which their \a initial_metadata_flags (as
  /// given in the call to \a grpc_lb_policy_pick_locked()) matches
  /// \a initial_metadata_flags_eq when ANDed with
  /// \a initial_metadata_flags_mask.
  virtual void CancelPicksLocked(uint32_t initial_metadata_flags_mask,
                                 uint32_t initial_metadata_flags_eq,
                                 grpc_error* error) GRPC_ABSTRACT;

  /// Tries to enter a READY connectivity state.
  virtual void ExitIdleLocked() GRPC_ABSTRACT;

  /// Calls \a closure when the connectivity state of the policy changes
  /// from \a *state.  Updates \a *state with the new state of the policy.
  virtual void NotifyOnStateChangeLocked(grpc_connectivity_state* state,
                                         grpc_closure* closure) GRPC_ABSTRACT;

  /// Returns the policy's current connectivity state.  Sets \a error to
  /// the associated error, if any.
  virtual grpc_connectivity_state CheckConnectivityLocked(
      grpc_error** connectivity_error) GRPC_ABSTRACT;

  /// Updates the policy with a new set of \a lb_policy_args.
  virtual void UpdateLocked(const Args& lb_policy_args) GRPC_ABSTRACT;

  /// Hands off pending picks to \a new_policy.
  virtual void HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy)
      GRPC_ABSTRACT;

  void Orphan() override {
    // Invoke ShutdownLocked() inside of the combiner.
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(&LoadBalancingPolicy::ShutdownAndUnrefLocked, this,
                            grpc_combiner_scheduler(combiner_)),
        GRPC_ERROR_NONE);
  }

  /// Sets the re-resolution closure to \a request_reresolution.
  void SetReresolutionClosureLocked(grpc_closure* request_reresolution) {
    GPR_ASSERT(request_reresolution_ == nullptr);
    request_reresolution_ = request_reresolution;
  }

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  /// Does NOT take ownership of the reference to \a combiner.
  // TODO(roth): Once we have a C++-like interface for combiners, this
  // API should change to take a smart pointer that does pass ownership
  // of a reference.
  explicit LoadBalancingPolicy(grpc_combiner* combiner);

  virtual ~LoadBalancingPolicy();

  grpc_combiner* combiner() const { return combiner_; }

  /// Shuts down the policy.  Any pending picks that have not been
  /// handed off to a new policy via HandOffPendingPicksLocked() will be
  /// failed.
  virtual void ShutdownLocked() GRPC_ABSTRACT;

  /// Tries to request a re-resolution.
// FIXME: add Locked suffix
  void TryReresolution(grpc_core::TraceFlag* grpc_lb_trace, grpc_error* error);

 private:
  static void ShutdownAndUnrefLocked(void* arg, grpc_error* ignored) {
    LoadBalancingPolicy* policy = static_cast<LoadBalancingPolicy*>(arg);
    policy->ShutdownLocked();
    policy->Unref();
  }

  /// Combiner under which LB policy actions take place.
  grpc_combiner* combiner_;
  /// Owned pointer to interested parties in load balancing decisions.
  grpc_pollset_set* interested_parties_;
  /// Callback to force a re-resolution.
  grpc_closure* request_reresolution_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_H */
