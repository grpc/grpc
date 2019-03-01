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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/service_config.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_lb_policy_refcount;

namespace grpc_core {

/// Interface for load balancing policies.
///
/// Note: All methods with a "Locked" suffix must be called from the
/// combiner passed to the constructor.
///
/// Any I/O done by the LB policy should be done under the pollset_set
/// returned by \a interested_parties().
// TODO(roth): Once we move to EventManager-based polling, remove the
// interested_parties() hooks from the API.
class LoadBalancingPolicy : public InternallyRefCounted<LoadBalancingPolicy> {
 public:
  /// State used for an LB pick.
  struct PickState {
    /// Initial metadata associated with the picking call.
    /// This is both an input and output parameter; the LB policy may
    /// use metadata here to influence its routing decision, and it may
    /// add new metadata here to be sent with the call to the chosen backend.
    grpc_metadata_batch* initial_metadata = nullptr;
    /// Storage for LB token in \a initial_metadata, or nullptr if not used.
    // TODO(roth): Remove this from the API.  Maybe have the LB policy
    // allocate this on the arena instead?
    grpc_linked_mdelem lb_token_mdelem_storage;
    /// Callback set by lb policy to be notified of trailing metadata.
    /// The callback must be scheduled on grpc_schedule_on_exec_ctx.
    grpc_closure* recv_trailing_metadata_ready = nullptr;
    /// The address that will be set to point to the original
    /// recv_trailing_metadata_ready callback, to be invoked by the LB
    /// policy's recv_trailing_metadata_ready callback when complete.
    /// Must be non-null if recv_trailing_metadata_ready is non-null.
    grpc_closure** original_recv_trailing_metadata_ready = nullptr;
    /// If this is not nullptr, then the client channel will point it to the
    /// call's trailing metadata before invoking recv_trailing_metadata_ready.
    /// If this is nullptr, then the callback will still be called.
    /// The lb does not have ownership of the metadata.
    grpc_metadata_batch** recv_trailing_metadata = nullptr;
    /// Will be set to the selected subchannel, or nullptr on failure or when
    /// the LB policy decides to drop the call.
    RefCountedPtr<ConnectedSubchannel> connected_subchannel;
    /// Will be populated with context to pass to the subchannel call, if
    /// needed.
    // TODO(roth): Remove this from the API, especially since it's not
    // working properly anyway (see https://github.com/grpc/grpc/issues/15927).
    grpc_call_context_element subchannel_call_context[GRPC_CONTEXT_COUNT] = {};
  };

  /// A picker is the object used to actual perform picks.
  ///
  /// Pickers are intended to encapsulate all of the state and logic
  /// needed on the data plane (i.e., to actually process picks for
  /// individual RPCs sent on the channel) while excluding all of the
  /// state and logic needed on the control plane (i.e., resolver
  /// updates, connectivity state notifications, etc); the latter should
  /// live in the LB policy object itself.
  ///
  /// Currently, pickers are always accessed from within the
  /// client_channel combiner, so they do not have to be thread-safe.
  // TODO(roth): In a subsequent PR, split the data plane work (i.e.,
  // the interaction with the picker) and the control plane work (i.e.,
  // the interaction with the LB policy) into two different
  // synchronization mechanisms, to avoid lock contention between the two.
  class SubchannelPicker {
   public:
    enum PickResult {
      // Pick complete.  If connected_subchannel is non-null, client channel
      // can immediately proceed with the call on connected_subchannel;
      // otherwise, call should be dropped.
      PICK_COMPLETE,
      // Pick cannot be completed until something changes on the control
      // plane.  Client channel will queue the pick and try again the
      // next time the picker is updated.
      PICK_QUEUE,
      // LB policy is in transient failure.  If the pick is wait_for_ready,
      // client channel will wait for the next picker and try again;
      // otherwise, the call will be failed immediately (although it may
      // be retried if the client channel is configured to do so).
      // The Pick() method will set its error parameter if this value is
      // returned.
      PICK_TRANSIENT_FAILURE,
    };

    SubchannelPicker() = default;
    virtual ~SubchannelPicker() = default;

    virtual PickResult Pick(PickState* pick, grpc_error** error) GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  // A picker that returns PICK_QUEUE for all picks.
  // Also calls the parent LB policy's ExitIdleLocked() method when the
  // first pick is seen.
  class QueuePicker : public SubchannelPicker {
   public:
    explicit QueuePicker(RefCountedPtr<LoadBalancingPolicy> parent)
        : parent_(std::move(parent)) {}

    PickResult Pick(PickState* pick, grpc_error** error) override {
      // We invoke the parent's ExitIdleLocked() via a closure instead
      // of doing it directly here, for two reasons:
      // 1. ExitIdleLocked() may cause the policy's state to change and
      //    a new picker to be delivered to the channel.  If that new
      //    picker is delivered before ExitIdleLocked() returns, then by
      //    the time this function returns, the pick will already have
      //    been processed, and we'll be trying to re-process the same
      //    pick again, leading to a crash.
      // 2. In a subsequent PR, we will split the data plane and control
      //    plane synchronization into separate combiners, at which
      //    point this will need to hop from the data plane combiner into
      //    the control plane combiner.
      if (!exit_idle_called_) {
        exit_idle_called_ = true;
        parent_->Ref().release();  // ref held by closure.
        GRPC_CLOSURE_SCHED(
            GRPC_CLOSURE_CREATE(&CallExitIdle, parent_.get(),
                                grpc_combiner_scheduler(parent_->combiner())),
            GRPC_ERROR_NONE);
      }
      return PICK_QUEUE;
    }

   private:
    static void CallExitIdle(void* arg, grpc_error* error) {
      LoadBalancingPolicy* parent = static_cast<LoadBalancingPolicy*>(arg);
      parent->ExitIdleLocked();
      parent->Unref();
    }

    RefCountedPtr<LoadBalancingPolicy> parent_;
    bool exit_idle_called_ = false;
  };

  // A picker that returns PICK_TRANSIENT_FAILURE for all picks.
  class TransientFailurePicker : public SubchannelPicker {
   public:
    explicit TransientFailurePicker(grpc_error* error) : error_(error) {}
    ~TransientFailurePicker() { GRPC_ERROR_UNREF(error_); }

    PickResult Pick(PickState* pick, grpc_error** error) override {
      *error = GRPC_ERROR_REF(error_);
      return PICK_TRANSIENT_FAILURE;
    }

   private:
    grpc_error* error_;
  };

  /// A proxy object used by the LB policy to communicate with the client
  /// channel.
  class ChannelControlHelper {
   public:
    ChannelControlHelper() = default;
    virtual ~ChannelControlHelper() = default;

    /// Creates a new subchannel with the specified channel args.
    virtual Subchannel* CreateSubchannel(const grpc_channel_args& args)
        GRPC_ABSTRACT;

    /// Creates a channel with the specified target, type, and channel args.
    virtual grpc_channel* CreateChannel(
        const char* target, grpc_client_channel_type type,
        const grpc_channel_args& args) GRPC_ABSTRACT;

    /// Sets the connectivity state and returns a new picker to be used
    /// by the client channel.
    virtual void UpdateState(grpc_connectivity_state state,
                             grpc_error* state_error,
                             UniquePtr<SubchannelPicker>) GRPC_ABSTRACT;

    /// Requests that the resolver re-resolve.
    virtual void RequestReresolution() GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  // Configuration for an LB policy instance.
  class Config : public RefCounted<Config> {
   public:
    Config(const grpc_json* lb_config,
           RefCountedPtr<ServiceConfig> service_config)
        : json_(lb_config), service_config_(std::move(service_config)) {}

    const grpc_json* json() const { return json_; }
    RefCountedPtr<ServiceConfig> service_config() const {
      return service_config_;
    }

   private:
    const grpc_json* json_;
    RefCountedPtr<ServiceConfig> service_config_;
  };

  /// Args used to instantiate an LB policy.
  struct Args {
    /// The combiner under which all LB policy calls will be run.
    /// Policy does NOT take ownership of the reference to the combiner.
    // TODO(roth): Once we have a C++-like interface for combiners, this
    // API should change to take a smart pointer that does pass ownership
    // of a reference.
    grpc_combiner* combiner = nullptr;
    /// Channel control helper.
    /// Note: LB policies MUST NOT call any method on the helper from
    /// their constructor.
    UniquePtr<ChannelControlHelper> channel_control_helper;
    /// Channel args.
    const grpc_channel_args* args = nullptr;
  };

  // Not copyable nor movable.
  LoadBalancingPolicy(const LoadBalancingPolicy&) = delete;
  LoadBalancingPolicy& operator=(const LoadBalancingPolicy&) = delete;

  /// Returns the name of the LB policy.
  virtual const char* name() const GRPC_ABSTRACT;

  /// Updates the policy with a new set of \a args and a new \a lb_config from
  /// the resolver. Will be invoked immediately after LB policy is constructed,
  /// and then again whenever the resolver returns a new result.
  /// Note that the LB policy gets the set of addresses from the
  /// GRPC_ARG_SERVER_ADDRESS_LIST channel arg.
  virtual void UpdateLocked(const grpc_channel_args& args,
                            RefCountedPtr<Config>)  // NOLINT
      GRPC_ABSTRACT;

  /// Tries to enter a READY connectivity state.
  /// This is a no-op by default, since most LB policies never go into
  /// IDLE state.
  virtual void ExitIdleLocked() {}

  /// Resets connection backoff.
  virtual void ResetBackoffLocked() GRPC_ABSTRACT;

  /// Populates child_subchannels and child_channels with the uuids of this
  /// LB policy's referenced children. This is not invoked from the
  /// client_channel's combiner. The implementation is responsible for
  /// providing its own synchronization.
  virtual void FillChildRefsForChannelz(
      channelz::ChildRefsList* child_subchannels,
      channelz::ChildRefsList* child_channels) GRPC_ABSTRACT;

  void Orphan() override {
    // Invoke ShutdownAndUnrefLocked() inside of the combiner.
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(&LoadBalancingPolicy::ShutdownAndUnrefLocked, this,
                            grpc_combiner_scheduler(combiner_)),
        GRPC_ERROR_NONE);
  }

  /// Returns the JSON node of policy (with both policy name and config content)
  /// given the JSON node of a LoadBalancingConfig array.
  static grpc_json* ParseLoadBalancingConfig(const grpc_json* lb_config_array);

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

  void set_channelz_node(
      RefCountedPtr<channelz::ClientChannelNode> channelz_node) {
    channelz_node_ = std::move(channelz_node);
  }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  explicit LoadBalancingPolicy(Args args, intptr_t initial_refcount = 1);
  virtual ~LoadBalancingPolicy();

  grpc_combiner* combiner() const { return combiner_; }

  // Note: LB policies MUST NOT call any method on the helper from
  // their constructor.
  // Note: This will return null after ShutdownLocked() has been called.
  ChannelControlHelper* channel_control_helper() const {
    return channel_control_helper_.get();
  }

  channelz::ClientChannelNode* channelz_node() const {
    return channelz_node_.get();
  }

  /// Shuts down the policy.  Any pending picks that have not been
  /// handed off to a new policy via HandOffPendingPicksLocked() will be
  /// failed.
  virtual void ShutdownLocked() GRPC_ABSTRACT;

 private:
  static void ShutdownAndUnrefLocked(void* arg, grpc_error* ignored) {
    LoadBalancingPolicy* policy = static_cast<LoadBalancingPolicy*>(arg);
    policy->ShutdownLocked();
    policy->channel_control_helper_.reset();
    policy->Unref();
  }

  /// Combiner under which LB policy actions take place.
  grpc_combiner* combiner_;
  /// Owned pointer to interested parties in load balancing decisions.
  grpc_pollset_set* interested_parties_;
  /// Channel control helper.
  UniquePtr<ChannelControlHelper> channel_control_helper_;
  /// Channelz node.
  RefCountedPtr<channelz::ClientChannelNode> channelz_node_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_H */
