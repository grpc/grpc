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

#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/subchannel_interface.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/string_view.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

extern DebugOnlyTraceFlag grpc_trace_lb_policy_refcount;

/// Interface for load balancing policies.
///
/// The following concepts are used here:
///
/// Channel: An abstraction that manages connections to backend servers
///   on behalf of a client application.  The application creates a channel
///   for a given server name and then sends calls (RPCs) on it, and the
///   channel figures out which backend server to send each call to.  A channel
///   contains a resolver, a load balancing policy (or a tree of LB policies),
///   and a set of one or more subchannels.
///
/// Subchannel: A subchannel represents a connection to one backend server.
///   The LB policy decides which subchannels to create, manages the
///   connectivity state of those subchannels, and decides which subchannel
///   to send any given call to.
///
/// Resolver: A plugin that takes a gRPC server URI and resolves it to a
///   list of one or more addresses and a service config, as described
///   in https://github.com/grpc/grpc/blob/master/doc/naming.md.  See
///   resolver.h for the resolver API.
///
/// Load Balancing (LB) Policy: A plugin that takes a list of addresses
///   from the resolver, maintains and manages a subchannel for each
///   backend address, and decides which subchannel to send each call on.
///   An LB policy has two parts:
///   - A LoadBalancingPolicy, which deals with the control plane work of
///     managing subchannels.
///   - A SubchannelPicker, which handles the data plane work of
///     determining which subchannel a given call should be sent on.

/// LoadBalacingPolicy API.
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
  // Represents backend metrics reported by the backend to the client.
  struct BackendMetricData {
    /// CPU utilization expressed as a fraction of available CPU resources.
    double cpu_utilization;
    /// Memory utilization expressed as a fraction of available memory
    /// resources.
    double mem_utilization;
    /// Total requests per second being served by the backend.  This
    /// should include all services that a backend is responsible for.
    uint64_t requests_per_second;
    /// Application-specific requests cost metrics.  Metric names are
    /// determined by the application.  Each value is an absolute cost
    /// (e.g. 3487 bytes of storage) associated with the request.
    Map<StringView, double, StringLess> request_cost;
    /// Application-specific resource utilization metrics.  Metric names
    /// are determined by the application.  Each value is expressed as a
    /// fraction of total resources available.
    Map<StringView, double, StringLess> utilization;
  };

  /// Interface for accessing per-call state.
  /// Implemented by the client channel and used by the SubchannelPicker.
  class CallState {
   public:
    CallState() = default;
    virtual ~CallState() = default;

    /// Allocates memory associated with the call, which will be
    /// automatically freed when the call is complete.
    /// It is more efficient to use this than to allocate memory directly
    /// for allocations that need to be made on a per-call basis.
    virtual void* Alloc(size_t size) GRPC_ABSTRACT;

    /// Returns the backend metric data returned by the server for the call,
    /// or null if no backend metric data was returned.
    virtual const BackendMetricData* GetBackendMetricData() GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  /// Interface for accessing metadata.
  /// Implemented by the client channel and used by the SubchannelPicker.
  class MetadataInterface {
   public:
    // Implementations whose iterators fit in intptr_t may internally
    // cast this directly to their iterator type.  Otherwise, they may
    // dynamically allocate their iterators and store the address here.
    typedef intptr_t Iterator;

    virtual ~MetadataInterface() = default;

    /// Adds a key/value pair.
    /// Does NOT take ownership of \a key or \a value.
    /// Implementations must ensure that the key and value remain alive
    /// until the call ends.  If desired, they may be allocated via
    /// CallState::Alloc().
    virtual void Add(StringView key, StringView value) GRPC_ABSTRACT;

    /// Iteration interface.
    virtual Iterator Begin() const GRPC_ABSTRACT;
    virtual bool IsEnd(Iterator it) const GRPC_ABSTRACT;
    virtual void Next(Iterator* it) const GRPC_ABSTRACT;
    virtual StringView Key(Iterator it) const GRPC_ABSTRACT;
    virtual StringView Value(Iterator it) const GRPC_ABSTRACT;

    /// Removes the element pointed to by \a it, which is modified to
    /// point to the next element.
    virtual void Erase(Iterator* it) GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  /// Arguments used when picking a subchannel for a call.
  struct PickArgs {
    /// Initial metadata associated with the picking call.
    /// The LB policy may use the existing metadata to influence its routing
    /// decision, and it may add new metadata elements to be sent with the
    /// call to the chosen backend.
    MetadataInterface* initial_metadata;
    /// An interface for accessing call state.  Can be used to allocate
    /// data associated with the call in an efficient way.
    CallState* call_state;
  };

  /// The result of picking a subchannel for a call.
  struct PickResult {
    enum ResultType {
      /// Pick complete.  If \a subchannel is non-null, the client channel
      /// will immediately proceed with the call on that subchannel;
      /// otherwise, it will drop the call.
      PICK_COMPLETE,
      /// Pick cannot be completed until something changes on the control
      /// plane.  The client channel will queue the pick and try again the
      /// next time the picker is updated.
      PICK_QUEUE,
      /// Pick failed.  If the call is wait_for_ready, the client channel
      /// will wait for the next picker and try again; otherwise, it
      /// will immediately fail the call with the status indicated via
      /// \a error (although the call may be retried if the client channel
      /// is configured to do so).
      PICK_FAILED,
    };
    ResultType type;

    /// Used only if type is PICK_COMPLETE.  Will be set to the selected
    /// subchannel, or nullptr if the LB policy decides to drop the call.
    RefCountedPtr<SubchannelInterface> subchannel;

    /// Used only if type is PICK_FAILED.
    /// Error to be set when returning a failure.
    // TODO(roth): Replace this with something similar to grpc::Status,
    // so that we don't expose grpc_error to this API.
    grpc_error* error = GRPC_ERROR_NONE;

    /// Used only if type is PICK_COMPLETE.
    /// Callback set by LB policy to be notified of trailing metadata.
    /// The user_data argument will be set to the
    /// recv_trailing_metadata_ready_user_data field.
    /// recv_trailing_metadata will be set to the metadata, which may be
    /// modified by the callback.  The callback does not take ownership,
    /// however, so any data that needs to be used after returning must
    /// be copied.
    /// call_state can be used to obtain backend metric data.
    // TODO(roth): Replace grpc_error with something better before we allow
    // people outside of gRPC team to use this API.
    void (*recv_trailing_metadata_ready)(
        void* user_data, grpc_error* error,
        MetadataInterface* recv_trailing_metadata,
        CallState* call_state) = nullptr;
    void* recv_trailing_metadata_ready_user_data = nullptr;
  };

  /// A subchannel picker is the object used to pick the subchannel to
  /// use for a given call.  This is implemented by the LB policy and
  /// used by the client channel to perform picks.
  ///
  /// Pickers are intended to encapsulate all of the state and logic
  /// needed on the data plane (i.e., to actually process picks for
  /// individual calls sent on the channel) while excluding all of the
  /// state and logic needed on the control plane (i.e., resolver
  /// updates, connectivity state notifications, etc); the latter should
  /// live in the LB policy object itself.
  ///
  /// Currently, pickers are always accessed from within the
  /// client_channel data plane combiner, so they do not have to be
  /// thread-safe.
  class SubchannelPicker {
   public:
    SubchannelPicker() = default;
    virtual ~SubchannelPicker() = default;

    virtual PickResult Pick(PickArgs args) GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  /// A proxy object implemented by the client channel and used by the
  /// LB policy to communicate with the channel.
  // TODO(juanlishen): Consider adding a mid-layer subclass that helps handle
  // things like swapping in pending policy when it's ready. Currently, we are
  // duplicating the logic in many subclasses.
  class ChannelControlHelper {
   public:
    ChannelControlHelper() = default;
    virtual ~ChannelControlHelper() = default;

    /// Creates a new subchannel with the specified channel args.
    virtual RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) GRPC_ABSTRACT;

    /// Sets the connectivity state and returns a new picker to be used
    /// by the client channel.
    virtual void UpdateState(grpc_connectivity_state state,
                             UniquePtr<SubchannelPicker>) GRPC_ABSTRACT;

    /// Requests that the resolver re-resolve.
    virtual void RequestReresolution() GRPC_ABSTRACT;

    /// Adds a trace message associated with the channel.
    enum TraceSeverity { TRACE_INFO, TRACE_WARNING, TRACE_ERROR };
    virtual void AddTraceEvent(TraceSeverity severity,
                               StringView message) GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  /// Interface for configuration data used by an LB policy implementation.
  /// Individual implementations will create a subclass that adds methods to
  /// return the parameters they need.
  class Config : public RefCounted<Config> {
   public:
    virtual ~Config() = default;

    // Returns the load balancing policy name
    virtual const char* name() const GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  /// Data passed to the UpdateLocked() method when new addresses and
  /// config are available.
  struct UpdateArgs {
    ServerAddressList addresses;
    RefCountedPtr<Config> config;
    const grpc_channel_args* args = nullptr;

    // TODO(roth): Remove everything below once channel args is
    // converted to a copyable and movable C++ object.
    UpdateArgs() = default;
    ~UpdateArgs() { grpc_channel_args_destroy(args); }
    UpdateArgs(const UpdateArgs& other);
    UpdateArgs(UpdateArgs&& other);
    UpdateArgs& operator=(const UpdateArgs& other);
    UpdateArgs& operator=(UpdateArgs&& other);
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
    // TODO(roth): Find a better channel args representation for this API.
    const grpc_channel_args* args = nullptr;
  };

  explicit LoadBalancingPolicy(Args args, intptr_t initial_refcount = 1);
  virtual ~LoadBalancingPolicy();

  // Not copyable nor movable.
  LoadBalancingPolicy(const LoadBalancingPolicy&) = delete;
  LoadBalancingPolicy& operator=(const LoadBalancingPolicy&) = delete;

  /// Returns the name of the LB policy.
  virtual const char* name() const GRPC_ABSTRACT;

  /// Updates the policy with new data from the resolver.  Will be invoked
  /// immediately after LB policy is constructed, and then again whenever
  /// the resolver returns a new result.
  virtual void UpdateLocked(UpdateArgs) GRPC_ABSTRACT;  // NOLINT

  /// Tries to enter a READY connectivity state.
  /// This is a no-op by default, since most LB policies never go into
  /// IDLE state.
  virtual void ExitIdleLocked() {}

  /// Resets connection backoff.
  virtual void ResetBackoffLocked() GRPC_ABSTRACT;

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

  // Note: This must be invoked while holding the combiner.
  void Orphan() override;

  // A picker that returns PICK_QUEUE for all picks.
  // Also calls the parent LB policy's ExitIdleLocked() method when the
  // first pick is seen.
  class QueuePicker : public SubchannelPicker {
   public:
    explicit QueuePicker(RefCountedPtr<LoadBalancingPolicy> parent)
        : parent_(std::move(parent)) {}

    ~QueuePicker() { parent_.reset(DEBUG_LOCATION, "QueuePicker"); }

    PickResult Pick(PickArgs args) override;

   private:
    static void CallExitIdle(void* arg, grpc_error* error);

    RefCountedPtr<LoadBalancingPolicy> parent_;
    bool exit_idle_called_ = false;
  };

  // A picker that returns PICK_TRANSIENT_FAILURE for all picks.
  class TransientFailurePicker : public SubchannelPicker {
   public:
    explicit TransientFailurePicker(grpc_error* error) : error_(error) {}
    ~TransientFailurePicker() override { GRPC_ERROR_UNREF(error_); }

    PickResult Pick(PickArgs args) override;

   private:
    grpc_error* error_;
  };

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  grpc_combiner* combiner() const { return combiner_; }

  // Note: LB policies MUST NOT call any method on the helper from their
  // constructor.
  ChannelControlHelper* channel_control_helper() const {
    return channel_control_helper_.get();
  }

  /// Shuts down the policy.
  virtual void ShutdownLocked() GRPC_ABSTRACT;

 private:
  /// Combiner under which LB policy actions take place.
  grpc_combiner* combiner_;
  /// Owned pointer to interested parties in load balancing decisions.
  grpc_pollset_set* interested_parties_;
  /// Channel control helper.
  UniquePtr<ChannelControlHelper> channel_control_helper_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_H */
