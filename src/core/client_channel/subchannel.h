//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <functional>
#include <map>
#include <memory>

#include "src/core/call/metadata_batch.h"
#include "src/core/client_channel/connector.h"
#include "src/core/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/time_precise.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/work_serializer.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"

// This arg is intended for internal use only, primarily for passing
// endpoint information during subchannel creation or connection.
#define GRPC_ARG_SUBCHANNEL_ENDPOINT "grpc.internal.subchannel_endpoint"

namespace grpc_core {

// A subchannel that knows how to connect to exactly one target address. It
// provides a target for load balancing.
//
// Note that this is the "real" subchannel implementation, whose API is
// different from the SubchannelInterface that is exposed to LB policy
// implementations.  The client channel provides an adaptor class
// (SubchannelWrapper) that "converts" between the two.
class Subchannel : public DualRefCounted<Subchannel> {
 public:
  class ConnectivityStateWatcherInterface
      : public RefCounted<ConnectivityStateWatcherInterface> {
   public:
    // Invoked whenever the subchannel's connectivity state changes.
    // There will be only one invocation of this method on a given watcher
    // instance at any given time.
    virtual void OnConnectivityStateChange(grpc_connectivity_state state,
                                           const absl::Status& status) = 0;

    // Invoked to report updated keepalive time.
    virtual void OnKeepaliveUpdate(Duration keepalive_time) = 0;

    virtual uint32_t max_connections_per_subchannel() const = 0;

    virtual grpc_pollset_set* interested_parties() = 0;
  };

  // A base class for producers of subchannel-specific data.
  // Implementations will typically add their own methods as needed.
  class DataProducerInterface : public DualRefCounted<DataProducerInterface> {
   public:
    // A unique identifier for this implementation.
    // Only one producer may be registered under a given type name on a
    // given subchannel at any given time.
    // Note that we use the pointer address instead of the string
    // contents for uniqueness; all instances for a given implementation
    // are expected to return the same string *instance*, not just the
    // same string contents.
    virtual UniqueTypeName type() const = 0;
  };

  // A call object for the v1 stack.
  // Provides the same interface as RefCounted<>.
  class Call {
   public:
    virtual ~Call() = default;

    // Continues processing a transport stream op batch.
    virtual void StartTransportStreamOpBatch(
        grpc_transport_stream_op_batch* batch) = 0;

    // Sets the 'then_schedule_closure' argument for call stack destruction.
    // Must be called once per call.
    virtual void SetAfterCallStackDestroy(grpc_closure* closure) = 0;

    // Interface of RefCounted<>.
    GRPC_MUST_USE_RESULT RefCountedPtr<Call> Ref();
    GRPC_MUST_USE_RESULT RefCountedPtr<Call> Ref(const DebugLocation& location,
                                                 const char* reason);
    virtual void Unref() = 0;
    virtual void Unref(const DebugLocation& location, const char* reason) = 0;

   private:
    // Allow RefCountedPtr<> to access IncrementRefCount().
    template <typename T>
    friend class RefCountedPtr;

    // Interface of RefCounted<>.
    virtual void IncrementRefCount() = 0;
    virtual void IncrementRefCount(const DebugLocation& location,
                                   const char* reason) = 0;
  };

  // Creates a subchannel.
  static RefCountedPtr<Subchannel> Create(
      OrphanablePtr<SubchannelConnector> connector,
      const grpc_resolved_address& address, const ChannelArgs& args);

  // Throttles keepalive time to \a new_keepalive_time iff \a new_keepalive_time
  // is larger than the subchannel's current keepalive time. The updated value
  // will have an affect when the subchannel creates a new ConnectedSubchannel.
  virtual void ThrottleKeepaliveTime(Duration new_keepalive_time) = 0;

  virtual grpc_pollset_set* pollset_set() const = 0;

  virtual channelz::SubchannelNode* channelz_node() = 0;

  virtual const ChannelArgs& args() const = 0;

  virtual std::string address() const = 0;

  // Starts watching the subchannel's connectivity state.
  // The first callback to the watcher will be delivered ~immediately.
  // Subsequent callbacks will be delivered as the subchannel's state
  // changes.
  // The watcher will be destroyed either when the subchannel is
  // destroyed or when CancelConnectivityStateWatch() is called.
  virtual void WatchConnectivityState(
      RefCountedPtr<ConnectivityStateWatcherInterface> watcher) = 0;

  // Cancels a connectivity state watch.
  // If the watcher has already been destroyed, this is a no-op.
  virtual void CancelConnectivityStateWatch(
      ConnectivityStateWatcherInterface* watcher) = 0;

  // Starts a call in the v1 stack.
  // Returns null if there is no connected subchannel.
  struct CreateCallArgs {
    grpc_polling_entity* pollent;
    gpr_cycle_counter start_time;
    Timestamp deadline;
    Arena* arena;
    CallCombiner* call_combiner;
  };
  virtual RefCountedPtr<Call> CreateCall(CreateCallArgs args,
                                         grpc_error_handle* error) = 0;

  // Used for calls in the v3 stack.
  virtual RefCountedPtr<UnstartedCallDestination> call_destination() = 0;

  // Attempt to connect to the backend.  Has no effect if already connected.
  virtual void RequestConnection() = 0;

  // Resets the connection backoff of the subchannel.
  virtual void ResetBackoff() = 0;

  // Access to data producer map.
  // We do not hold refs to the data producer; the implementation is
  // expected to register itself upon construction and remove itself
  // upon destruction.
  //
  // Looks up the current data producer for type and invokes get_or_add()
  // with a pointer to that producer in the map.  The get_or_add() function
  // can modify the pointed-to value to update the map.  This provides a
  // way to either re-use an existing producer or register a new one in
  // a non-racy way.
  virtual void GetOrAddDataProducer(
      UniqueTypeName type,
      std::function<void(DataProducerInterface**)> get_or_add) = 0;
  // Removes the data producer from the map, if the current producer for
  // this type is the specified producer.
  virtual void RemoveDataProducer(DataProducerInterface* data_producer) = 0;

  virtual std::shared_ptr<grpc_event_engine::experimental::EventEngine>
  event_engine() = 0;

  // Ping API for v3 stack.
  virtual void Ping(absl::AnyInvocable<void(absl::Status)> on_ack) = 0;
  // Ping API for v1 stack.
  // TODO(roth): Remove this when v3 migration is done.
  virtual absl::Status Ping(grpc_closure* on_initiate,
                            grpc_closure* on_ack) = 0;

  // Exposed for testing purposes only.
  static ChannelArgs MakeSubchannelArgs(
      const ChannelArgs& channel_args, const ChannelArgs& address_args,
      const RefCountedPtr<SubchannelPoolInterface>& subchannel_pool,
      const std::string& channel_default_authority);

 protected:
  Subchannel();
};

class NewSubchannel final : public Subchannel {
 public:
  // Creates a subchannel.
  static RefCountedPtr<Subchannel> Create(
      OrphanablePtr<SubchannelConnector> connector,
      const grpc_resolved_address& address, const ChannelArgs& args);

  // The ctor and dtor are not intended to use directly.
  // Use Subchannel::Create() instead.
  NewSubchannel(SubchannelKey key, OrphanablePtr<SubchannelConnector> connector,
                const ChannelArgs& args);
  ~NewSubchannel() override;

  // Throttles keepalive time to \a new_keepalive_time iff \a new_keepalive_time
  // is larger than the subchannel's current keepalive time. The updated value
  // will have an affect when the subchannel creates a new ConnectedSubchannel.
  void ThrottleKeepaliveTime(Duration new_keepalive_time) override
      ABSL_LOCKS_EXCLUDED(mu_);

  grpc_pollset_set* pollset_set() const override { return pollset_set_; }

  channelz::SubchannelNode* channelz_node() override;

  const ChannelArgs& args() const override { return args_; }

  std::string address() const override {
    return grpc_sockaddr_to_uri(&key_.address())
        .value_or("<unknown address type>");
  }

  // Starts watching the subchannel's connectivity state.
  // The first callback to the watcher will be delivered ~immediately.
  // Subsequent callbacks will be delivered as the subchannel's state
  // changes.
  // The watcher will be destroyed either when the subchannel is
  // destroyed or when CancelConnectivityStateWatch() is called.
  void WatchConnectivityState(
      RefCountedPtr<ConnectivityStateWatcherInterface> watcher) override
      ABSL_LOCKS_EXCLUDED(mu_);

  // Cancels a connectivity state watch.
  // If the watcher has already been destroyed, this is a no-op.
  void CancelConnectivityStateWatch(ConnectivityStateWatcherInterface* watcher)
      override ABSL_LOCKS_EXCLUDED(mu_);

  // Starts a call in the v1 stack.
  // Returns null if there is no connected subchannel.
  RefCountedPtr<Call> CreateCall(CreateCallArgs args,
                                 grpc_error_handle* error) override;

  // Used for calls in the v3 stack.
  RefCountedPtr<UnstartedCallDestination> call_destination() override;

  // Attempt to connect to the backend.  Has no effect if already connected.
  void RequestConnection() override ABSL_LOCKS_EXCLUDED(mu_);

  // Resets the connection backoff of the subchannel.
  void ResetBackoff() override ABSL_LOCKS_EXCLUDED(mu_);

  // Access to data producer map.
  // We do not hold refs to the data producer; the implementation is
  // expected to register itself upon construction and remove itself
  // upon destruction.
  //
  // Looks up the current data producer for type and invokes get_or_add()
  // with a pointer to that producer in the map.  The get_or_add() function
  // can modify the pointed-to value to update the map.  This provides a
  // way to either re-use an existing producer or register a new one in
  // a non-racy way.
  void GetOrAddDataProducer(
      UniqueTypeName type,
      std::function<void(DataProducerInterface**)> get_or_add) override
      ABSL_LOCKS_EXCLUDED(mu_);
  // Removes the data producer from the map, if the current producer for
  // this type is the specified producer.
  void RemoveDataProducer(DataProducerInterface* data_producer) override
      ABSL_LOCKS_EXCLUDED(mu_);

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine()
      override {
    return event_engine_;
  }

  // Ping API for v3 stack.
  void Ping(absl::AnyInvocable<void(absl::Status)> on_ack) override;
  // Ping API for v1 stack.
  // TODO(roth): Remove this when v3 migration is done.
  absl::Status Ping(grpc_closure* on_initiate, grpc_closure* on_ack) override;

 private:
  // A linked list of ConnectivityStateWatcherInterfaces that are monitoring
  // the subchannel's state.
  class ConnectivityStateWatcherList final {
   public:
    explicit ConnectivityStateWatcherList(NewSubchannel* subchannel)
        : subchannel_(subchannel) {}

    ~ConnectivityStateWatcherList() { Clear(); }

    void AddWatcherLocked(
        RefCountedPtr<ConnectivityStateWatcherInterface> watcher);
    void RemoveWatcherLocked(ConnectivityStateWatcherInterface* watcher);

    // Notifies all watchers in the list about a change to state.
    void NotifyLocked(grpc_connectivity_state state,
                      const absl::Status& status);

    // Notifies all watchers about a keepalive update.
    void NotifyOnKeepaliveUpdateLocked(Duration new_keepalive_time);

    void Clear() { watchers_.clear(); }

    bool empty() const { return watchers_.empty(); }

    uint32_t GetMaxConnectionsPerSubchannel() const;

   private:
    NewSubchannel* subchannel_;
    absl::flat_hash_set<RefCountedPtr<ConnectivityStateWatcherInterface>,
                        RefCountedPtrHash<ConnectivityStateWatcherInterface>,
                        RefCountedPtrEq<ConnectivityStateWatcherInterface>>
        watchers_;
  };

  class ConnectedSubchannel;
  class LegacyConnectedSubchannel;
  class NewConnectedSubchannel;

  // TODO(roth): Remove this when subchannel_connection_scaling experiment
  // is removed.
  class ConnectedSubchannelStateWatcher;

  class ConnectionStateWatcher;

  class QueuedCall;

  // Tears down any existing connection, and arranges for destruction
  void Orphaned() override ABSL_LOCKS_EXCLUDED(mu_);

  RefCountedPtr<ConnectedSubchannel> ChooseConnectionLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void RetryQueuedRpcs() ABSL_LOCKS_EXCLUDED(mu_);
  void RetryQueuedRpcsLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void FailAllQueuedRpcsLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Updates the subchannel's connectivity state.
  void SetLastFailureLocked(const absl::Status& status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  grpc_connectivity_state ComputeConnectivityStateLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  absl::Status ConnectivityStatusToReportLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void MaybeUpdateConnectivityStateLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Returns true if the connection was removed.
  bool RemoveConnectionLocked(ConnectedSubchannel* connected_subchannel)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void ThrottleKeepaliveTimeLocked(Duration new_keepalive_time)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Methods for connection.
  void OnRetryTimer() ABSL_LOCKS_EXCLUDED(mu_);
  void OnRetryTimerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void StartConnectingLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void OnConnectingFinished(void* arg, grpc_error_handle error)
      ABSL_LOCKS_EXCLUDED(mu_);
  void OnConnectingFinishedLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool PublishTransportLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // The subchannel pool this subchannel is in.
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_;
  // Subchannel key that identifies this subchannel in the subchannel pool.
  const SubchannelKey key_;
  // boolean value that identifies this subchannel is created from event engine
  // endpoint.
  const bool created_from_endpoint_;
  // Actual address to connect to.  May be different than the address in
  // key_ if overridden by proxy mapper.
  grpc_resolved_address address_for_connect_;
  // Channel args.
  ChannelArgs args_;
  // pollset_set tracking who's interested in a connection being setup.
  grpc_pollset_set* pollset_set_;
  // Channelz tracking.
  RefCountedPtr<channelz::SubchannelNode> channelz_node_;
  // Minimum connection timeout.
  Duration min_connect_timeout_;

  // Connection state.
  OrphanablePtr<SubchannelConnector> connector_;
  SubchannelConnector::Result connecting_result_;
  grpc_closure on_connecting_finished_;

  // Protects the other members.
  Mutex mu_;

  bool connection_attempt_in_flight_ ABSL_GUARDED_BY(mu_) = false;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;

  // Connectivity state tracking.
  // Note that the connectivity state implies the state of the
  // Subchannel object:
  // - IDLE: no retry timer pending, can start a connection attempt at any time
  // - CONNECTING: connection attempt in progress
  // - READY: connection attempt succeeded, connected_subchannel_ created
  // - TRANSIENT_FAILURE: connection attempt failed, retry timer pending
  grpc_connectivity_state state_ ABSL_GUARDED_BY(mu_) = GRPC_CHANNEL_IDLE;
  absl::Status last_failure_status_ ABSL_GUARDED_BY(mu_);
  // The list of connectivity state watchers.
  ConnectivityStateWatcherList watcher_list_ ABSL_GUARDED_BY(mu_);
  // Used for sending connectivity state notifications.
  WorkSerializer work_serializer_;

  // Established connections.
  std::vector<RefCountedPtr<ConnectedSubchannel>> connections_
      ABSL_GUARDED_BY(mu_);

  // Backoff state.
  BackOff backoff_ ABSL_GUARDED_BY(mu_);
  Timestamp next_attempt_time_ ABSL_GUARDED_BY(mu_);
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      retry_timer_handle_ ABSL_GUARDED_BY(mu_);

  // Keepalive time period
  Duration keepalive_time_ ABSL_GUARDED_BY(mu_);

  // Data producer map.
  std::map<UniqueTypeName, DataProducerInterface*> data_producer_map_
      ABSL_GUARDED_BY(mu_);
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;

  // Entries will be null for calls that have been cancelled.
  std::deque<QueuedCall*> queued_calls_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_H
