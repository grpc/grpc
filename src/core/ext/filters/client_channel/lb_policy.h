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
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
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
class LoadBalancingPolicy : public InternallyRefCounted<LoadBalancingPolicy> {
 public:
  /// State used for an LB pick.
  struct PickState {
    /// Initial metadata associated with the picking call.
    grpc_metadata_batch* initial_metadata = nullptr;
    /// Storage for LB token in \a initial_metadata, or nullptr if not used.
    grpc_linked_mdelem lb_token_mdelem_storage;
    // Callback set by lb policy to be notified of trailing metadata.
    // The callback must be scheduled on grpc_schedule_on_exec_ctx.
    grpc_closure* recv_trailing_metadata_ready = nullptr;
    // The address that will be set to point to the original
    // recv_trailing_metadata_ready callback, to be invoked by the LB
    // policy's recv_trailing_metadata_ready callback when complete.
    // Must be non-null if recv_trailing_metadata_ready is non-null.
    grpc_closure** original_recv_trailing_metadata_ready = nullptr;
    // If this is not nullptr, then the client channel will point it to the
    // call's trailing metadata before invoking recv_trailing_metadata_ready.
    // If this is nullptr, then the callback will still be called.
    // The lb does not have ownership of the metadata.
    grpc_metadata_batch** recv_trailing_metadata = nullptr;
    /// Will be set to the selected subchannel, or nullptr on failure or when
    /// the LB policy decides to drop the call.
    RefCountedPtr<ConnectedSubchannel> connected_subchannel;
    /// Will be populated with context to pass to the subchannel call, if
    /// needed.
    grpc_call_context_element subchannel_call_context[GRPC_CONTEXT_COUNT] = {};
  };

// FIXME: document
  class SubchannelPicker : public RefCounted<SubchannelPicker> {
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
      // otherwise, the call will be failed immediately.
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
  // Also calls the parent LB policy's ExitIdleLocked() method.
  class QueuePicker : public SubchannelPicker {
   public:
    explicit QueuePicker(RefCountedPtr<LoadBalancingPolicy> parent)
        : parent_(std::move(parent)) {
      GRPC_CLOSURE_INIT(&exit_idle_closure_, &CallExitIdle, this,
                        grpc_combiner_scheduler(parent_->combiner()));
    }

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
        Ref().release();  // ref held by closure.
        GRPC_CLOSURE_SCHED(&exit_idle_closure_, GRPC_ERROR_NONE);
      }
      return PICK_QUEUE;
    }

   private:
    static void CallExitIdle(void* arg, grpc_error* error) {
      QueuePicker* self = static_cast<QueuePicker*>(arg);
      self->parent_->ExitIdleLocked();
      self->Unref();
    }

    RefCountedPtr<LoadBalancingPolicy> parent_;
    bool exit_idle_called_ = false;
    grpc_closure exit_idle_closure_;
  };

  // A picker that returns PICK_TRANSIENT_FAILURE for all picks.
  class TransientFailurePicker : public SubchannelPicker {
   public:
    explicit TransientFailurePicker(grpc_error* error)
        : error_(error) {}
    ~TransientFailurePicker() { GRPC_ERROR_UNREF(error_); }

    PickResult Pick(PickState* pick, grpc_error** error) override {
      *error = GRPC_ERROR_REF(error_);
      return PICK_TRANSIENT_FAILURE;
    }

   private:
    grpc_error* error_;
  };

// FIXME: document
  class ChannelControlHelper : public RefCounted<ChannelControlHelper> {
   public:
    ChannelControlHelper() = default;
    virtual ~ChannelControlHelper() = default;

    virtual void UpdateState(
        grpc_connectivity_state state, grpc_error* state_error,
        RefCountedPtr<SubchannelPicker> picker) GRPC_ABSTRACT;

    virtual void RequestReresolution() GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  struct Args {
    /// The combiner under which all LB policy calls will be run.
    /// Policy does NOT take ownership of the reference to the combiner.
    // TODO(roth): Once we have a C++-like interface for combiners, this
    // API should change to take a smart pointer that does pass ownership
    // of a reference.
    grpc_combiner* combiner = nullptr;
    /// Used to create channels and subchannels.
    grpc_client_channel_factory* client_channel_factory = nullptr;
    /// Subchannel pool.
    RefCountedPtr<SubchannelPoolInterface> subchannel_pool;
    /// Channel control helper.
    RefCountedPtr<ChannelControlHelper> channel_control_helper;
    /// Channel args from the resolver.
    /// Note that the LB policy gets the set of addresses from the
    /// GRPC_ARG_SERVER_ADDRESS_LIST channel arg.
    const grpc_channel_args* args = nullptr;
    /// Load balancing config from the resolver.
    grpc_json* lb_config = nullptr;
  };

  // Not copyable nor movable.
  LoadBalancingPolicy(const LoadBalancingPolicy&) = delete;
  LoadBalancingPolicy& operator=(const LoadBalancingPolicy&) = delete;

  /// Returns the name of the LB policy.
  virtual const char* name() const GRPC_ABSTRACT;

  /// Updates the policy with a new set of \a args and a new \a lb_config from
  /// the resolver. Note that the LB policy gets the set of addresses from the
  /// GRPC_ARG_SERVER_ADDRESS_LIST channel arg.
  virtual void UpdateLocked(const grpc_channel_args& args,
                            grpc_json* lb_config) GRPC_ABSTRACT;

  /// Tries to enter a READY connectivity state.
  /// TODO(roth): As part of restructuring how we handle IDLE state,
  /// consider whether this method is still needed.
  virtual void ExitIdleLocked() GRPC_ABSTRACT;

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

  SubchannelPoolInterface* subchannel_pool() const {
    return subchannel_pool_.get();
  }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  explicit LoadBalancingPolicy(Args args);
  virtual ~LoadBalancingPolicy();

  grpc_combiner* combiner() const { return combiner_; }
  grpc_client_channel_factory* client_channel_factory() const {
    return client_channel_factory_;
  }

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
  /// Client channel factory, used to create channels and subchannels.
  grpc_client_channel_factory* client_channel_factory_;
  /// Subchannel pool.
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_;
  /// Owned pointer to interested parties in load balancing decisions.
  grpc_pollset_set* interested_parties_;
  /// Channel control helper.
  RefCountedPtr<ChannelControlHelper> channel_control_helper_;
  /// Channelz node.
  RefCountedPtr<channelz::ClientChannelNode> channelz_node_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_H */
