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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H

#include <grpc/support/port_platform.h>

#include <deque>

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata.h"

// Channel arg containing a URI indicating the address to connect to.
#define GRPC_ARG_SUBCHANNEL_ADDRESS "grpc.subchannel_address"

namespace grpc_core {

class SubchannelCall;

class ConnectedSubchannel : public RefCounted<ConnectedSubchannel> {
 public:
  ConnectedSubchannel(
      grpc_channel_stack* channel_stack, const grpc_channel_args* args,
      RefCountedPtr<channelz::SubchannelNode> channelz_subchannel);
  ~ConnectedSubchannel() override;

  void StartWatch(grpc_pollset_set* interested_parties,
                  OrphanablePtr<ConnectivityStateWatcherInterface> watcher);

  void Ping(grpc_closure* on_initiate, grpc_closure* on_ack);

  grpc_channel_stack* channel_stack() const { return channel_stack_; }
  const grpc_channel_args* args() const { return args_; }
  channelz::SubchannelNode* channelz_subchannel() const {
    return channelz_subchannel_.get();
  }

  size_t GetInitialCallSizeEstimate() const;

 private:
  grpc_channel_stack* channel_stack_;
  grpc_channel_args* args_;
  // ref counted pointer to the channelz node in this connected subchannel's
  // owning subchannel.
  RefCountedPtr<channelz::SubchannelNode> channelz_subchannel_;
};

// Implements the interface of RefCounted<>.
class SubchannelCall {
 public:
  struct Args {
    RefCountedPtr<ConnectedSubchannel> connected_subchannel;
    grpc_polling_entity* pollent;
    grpc_slice path;
    gpr_cycle_counter start_time;
    grpc_millis deadline;
    Arena* arena;
    grpc_call_context_element* context;
    CallCombiner* call_combiner;
  };
  static RefCountedPtr<SubchannelCall> Create(Args args,
                                              grpc_error_handle* error);

  // Continues processing a transport stream op batch.
  void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

  // Returns the call stack of the subchannel call.
  grpc_call_stack* GetCallStack();

  // Sets the 'then_schedule_closure' argument for call stack destruction.
  // Must be called once per call.
  void SetAfterCallStackDestroy(grpc_closure* closure);

  // Interface of RefCounted<>.
  RefCountedPtr<SubchannelCall> Ref() GRPC_MUST_USE_RESULT;
  RefCountedPtr<SubchannelCall> Ref(const DebugLocation& location,
                                    const char* reason) GRPC_MUST_USE_RESULT;
  // When refcount drops to 0, destroys itself and the associated call stack,
  // but does NOT free the memory because it's in the call arena.
  void Unref();
  void Unref(const DebugLocation& location, const char* reason);

 private:
  // Allow RefCountedPtr<> to access IncrementRefCount().
  template <typename T>
  friend class RefCountedPtr;

  SubchannelCall(Args args, grpc_error_handle* error);

  // If channelz is enabled, intercepts recv_trailing so that we may check the
  // status and associate it to a subchannel.
  void MaybeInterceptRecvTrailingMetadata(
      grpc_transport_stream_op_batch* batch);

  static void RecvTrailingMetadataReady(void* arg, grpc_error_handle error);

  // Interface of RefCounted<>.
  void IncrementRefCount();
  void IncrementRefCount(const DebugLocation& location, const char* reason);

  static void Destroy(void* arg, grpc_error_handle error);

  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
  grpc_closure* after_call_stack_destroy_ = nullptr;
  // State needed to support channelz interception of recv trailing metadata.
  grpc_closure recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ = nullptr;
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  grpc_millis deadline_;
};

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
    struct ConnectivityStateChange {
      grpc_connectivity_state state;
      absl::Status status;
      RefCountedPtr<ConnectedSubchannel> connected_subchannel;
    };

    ~ConnectivityStateWatcherInterface() override = default;

    // Will be invoked whenever the subchannel's connectivity state
    // changes.  There will be only one invocation of this method on a
    // given watcher instance at any given time.
    // Implementations should call PopConnectivityStateChange to get the next
    // connectivity state change.
    virtual void OnConnectivityStateChange() = 0;

    virtual grpc_pollset_set* interested_parties() = 0;

    // Enqueues connectivity state change notifications.
    // When the state changes to READY, connected_subchannel will
    // contain a ref to the connected subchannel.  When it changes from
    // READY to some other state, the implementation must release its
    // ref to the connected subchannel.
    // TODO(yashkt): This is currently needed to send the state updates in the
    // right order when asynchronously notifying. This will no longer be
    // necessary when we have access to EventManager.
    void PushConnectivityStateChange(ConnectivityStateChange state_change);

    // Dequeues connectivity state change notifications.
    ConnectivityStateChange PopConnectivityStateChange();

   private:
    Mutex mu_;  // protects the queue
    // Keeps track of the updates that the watcher instance must be notified of.
    // TODO(yashkt): This is currently needed to send the state updates in the
    // right order when asynchronously notifying. This will no longer be
    // necessary when we have access to EventManager.
    std::deque<ConnectivityStateChange> connectivity_state_queue_
        ABSL_GUARDED_BY(&mu_);
  };

  // Creates a subchannel given \a connector and \a args.
  static RefCountedPtr<Subchannel> Create(
      OrphanablePtr<SubchannelConnector> connector,
      const grpc_channel_args* args);

  // The ctor and dtor are not intended to use directly.
  Subchannel(SubchannelKey key, OrphanablePtr<SubchannelConnector> connector,
             const grpc_channel_args* args);
  ~Subchannel() override;

  // Throttles keepalive time to \a new_keepalive_time iff \a new_keepalive_time
  // is larger than the subchannel's current keepalive time. The updated value
  // will have an affect when the subchannel creates a new ConnectedSubchannel.
  void ThrottleKeepaliveTime(int new_keepalive_time) ABSL_LOCKS_EXCLUDED(mu_);

  // Gets the string representing the subchannel address.
  // Caller doesn't take ownership.
  const char* GetTargetAddress();

  const grpc_channel_args* channel_args() const { return args_; }

  channelz::SubchannelNode* channelz_node();

  // Returns the current connectivity state of the subchannel.
  // If health_check_service_name is non-null, the returned connectivity
  // state will be based on the state reported by the backend for that
  // service name.
  // If the return value is GRPC_CHANNEL_READY, also sets *connected_subchannel.
  grpc_connectivity_state CheckConnectivityState(
      const absl::optional<std::string>& health_check_service_name,
      RefCountedPtr<ConnectedSubchannel>* connected_subchannel)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Starts watching the subchannel's connectivity state.
  // The first callback to the watcher will be delivered when the
  // subchannel's connectivity state becomes a value other than
  // initial_state, which may happen immediately.
  // Subsequent callbacks will be delivered as the subchannel's state
  // changes.
  // The watcher will be destroyed either when the subchannel is
  // destroyed or when CancelConnectivityStateWatch() is called.
  void WatchConnectivityState(
      grpc_connectivity_state initial_state,
      const absl::optional<std::string>& health_check_service_name,
      RefCountedPtr<ConnectivityStateWatcherInterface> watcher)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Cancels a connectivity state watch.
  // If the watcher has already been destroyed, this is a no-op.
  void CancelConnectivityStateWatch(
      const absl::optional<std::string>& health_check_service_name,
      ConnectivityStateWatcherInterface* watcher) ABSL_LOCKS_EXCLUDED(mu_);

  // Attempt to connect to the backend.  Has no effect if already connected.
  void AttemptToConnect() ABSL_LOCKS_EXCLUDED(mu_);

  // Resets the connection backoff of the subchannel.
  // TODO(roth): Move connection backoff out of subchannels and up into LB
  // policy code (probably by adding a SubchannelGroup between
  // SubchannelList and SubchannelData), at which point this method can
  // go away.
  void ResetBackoff() ABSL_LOCKS_EXCLUDED(mu_);

  // Tears down any existing connection, and arranges for destruction
  void Orphan() override ABSL_LOCKS_EXCLUDED(mu_);

  // Returns a new channel arg encoding the subchannel address as a URI
  // string. Caller is responsible for freeing the string.
  static grpc_arg CreateSubchannelAddressArg(const grpc_resolved_address* addr);

  // Returns the URI string from the subchannel address arg in \a args.
  static const char* GetUriFromSubchannelAddressArg(
      const grpc_channel_args* args);

  // Sets \a addr from the subchannel address arg in \a args.
  static void GetAddressFromSubchannelAddressArg(const grpc_channel_args* args,
                                                 grpc_resolved_address* addr);

 private:
  // A linked list of ConnectivityStateWatcherInterfaces that are monitoring
  // the subchannel's state.
  class ConnectivityStateWatcherList {
   public:
    ~ConnectivityStateWatcherList() { Clear(); }

    void AddWatcherLocked(
        RefCountedPtr<ConnectivityStateWatcherInterface> watcher);
    void RemoveWatcherLocked(ConnectivityStateWatcherInterface* watcher);

    // Notifies all watchers in the list about a change to state.
    void NotifyLocked(Subchannel* subchannel, grpc_connectivity_state state,
                      const absl::Status& status);

    void Clear() { watchers_.clear(); }

    bool empty() const { return watchers_.empty(); }

   private:
    // TODO(roth): Once we can use C++-14 heterogeneous lookups, this can
    // be a set instead of a map.
    std::map<ConnectivityStateWatcherInterface*,
             RefCountedPtr<ConnectivityStateWatcherInterface>>
        watchers_;
  };

  // A map that tracks ConnectivityStateWatcherInterfaces using a particular
  // health check service name.
  //
  // There is one entry in the map for each health check service name.
  // Entries exist only as long as there are watchers using the
  // corresponding service name.
  //
  // A health check client is maintained only while the subchannel is in
  // state READY.
  class HealthWatcherMap {
   public:
    void AddWatcherLocked(
        WeakRefCountedPtr<Subchannel> subchannel,
        grpc_connectivity_state initial_state,
        const std::string& health_check_service_name,
        RefCountedPtr<ConnectivityStateWatcherInterface> watcher);
    void RemoveWatcherLocked(const std::string& health_check_service_name,
                             ConnectivityStateWatcherInterface* watcher);

    // Notifies the watcher when the subchannel's state changes.
    void NotifyLocked(grpc_connectivity_state state, const absl::Status& status)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Subchannel::mu_);

    grpc_connectivity_state CheckConnectivityStateLocked(
        Subchannel* subchannel, const std::string& health_check_service_name)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Subchannel::mu_);

    void ShutdownLocked();

   private:
    class HealthWatcher;

    std::map<std::string, OrphanablePtr<HealthWatcher>> map_;
  };

  class ConnectedSubchannelStateWatcher;

  class AsyncWatcherNotifierLocked;

  // Sets the subchannel's connectivity state to \a state.
  void SetConnectivityStateLocked(grpc_connectivity_state state,
                                  const absl::Status& status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Methods for connection.
  void MaybeStartConnectingLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void OnRetryAlarm(void* arg, grpc_error_handle error)
      ABSL_LOCKS_EXCLUDED(mu_);
  void ContinueConnectingLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void OnConnectingFinished(void* arg, grpc_error_handle error)
      ABSL_LOCKS_EXCLUDED(mu_);
  bool PublishTransportLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // The subchannel pool this subchannel is in.
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_;
  // TODO(juanlishen): Consider using args_ as key_ directly.
  // Subchannel key that identifies this subchannel in the subchannel pool.
  const SubchannelKey key_;
  // Channel args.
  grpc_channel_args* args_;
  // pollset_set tracking who's interested in a connection being setup.
  grpc_pollset_set* pollset_set_;
  // Channelz tracking.
  RefCountedPtr<channelz::SubchannelNode> channelz_node_;

  // Connection state.
  OrphanablePtr<SubchannelConnector> connector_;
  SubchannelConnector::Result connecting_result_;
  grpc_closure on_connecting_finished_;

  // Protects the other members.
  Mutex mu_;

  // Active connection, or null.
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_ ABSL_GUARDED_BY(mu_);
  bool connecting_ ABSL_GUARDED_BY(mu_) = false;
  bool disconnected_ ABSL_GUARDED_BY(mu_) = false;

  // Connectivity state tracking.
  grpc_connectivity_state state_ ABSL_GUARDED_BY(mu_) = GRPC_CHANNEL_IDLE;
  absl::Status status_ ABSL_GUARDED_BY(mu_);
  // The list of watchers without a health check service name.
  ConnectivityStateWatcherList watcher_list_ ABSL_GUARDED_BY(mu_);
  // The map of watchers with health check service names.
  HealthWatcherMap health_watcher_map_ ABSL_GUARDED_BY(mu_);

  // Backoff state.
  BackOff backoff_ ABSL_GUARDED_BY(mu_);
  grpc_millis next_attempt_deadline_ ABSL_GUARDED_BY(mu_);
  grpc_millis min_connect_timeout_ms_ ABSL_GUARDED_BY(mu_);
  bool backoff_begun_ ABSL_GUARDED_BY(mu_) = false;

  // Retry alarm.
  grpc_timer retry_alarm_ ABSL_GUARDED_BY(mu_);
  grpc_closure on_retry_alarm_ ABSL_GUARDED_BY(mu_);
  bool have_retry_alarm_ ABSL_GUARDED_BY(mu_) = false;
  // reset_backoff() was called while alarm was pending.
  bool retry_immediately_ ABSL_GUARDED_BY(mu_) = false;
  // Keepalive time period (-1 for unset)
  int keepalive_time_ ABSL_GUARDED_BY(mu_) = -1;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H */
