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

#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/filters/client_channel/subchannel_interface.h"
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata.h"

// Channel arg containing a grpc_resolved_address to connect to.
#define GRPC_ARG_SUBCHANNEL_ADDRESS "grpc.subchannel_address"

// For debugging refcounting.
#ifndef NDEBUG
#define GRPC_SUBCHANNEL_REF(p, r) (p)->Ref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) \
  (p)->RefFromWeakRef(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_UNREF(p, r) (p)->Unref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) (p)->WeakRef(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_WEAK_UNREF(p, r) (p)->WeakUnref(__FILE__, __LINE__, (r))
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS \
  const char *file, int line, const char *reason
#define GRPC_SUBCHANNEL_REF_REASON reason
#define GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS \
  , GRPC_SUBCHANNEL_REF_EXTRA_ARGS, const char* purpose
#define GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE(x) , file, line, reason, x
#else
#define GRPC_SUBCHANNEL_REF(p, r) (p)->Ref()
#define GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(p, r) (p)->RefFromWeakRef()
#define GRPC_SUBCHANNEL_UNREF(p, r) (p)->Unref()
#define GRPC_SUBCHANNEL_WEAK_REF(p, r) (p)->WeakRef()
#define GRPC_SUBCHANNEL_WEAK_UNREF(p, r) (p)->WeakUnref()
#define GRPC_SUBCHANNEL_REF_EXTRA_ARGS
#define GRPC_SUBCHANNEL_REF_REASON ""
#define GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS
#define GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE(x)
#endif

namespace grpc_core {

class SubchannelCall;

class ConnectedSubchannel : public ConnectedSubchannelInterface {
 public:
  struct CallArgs {
    grpc_polling_entity* pollent;
    grpc_slice path;
    gpr_timespec start_time;
    grpc_millis deadline;
    Arena* arena;
    grpc_call_context_element* context;
    CallCombiner* call_combiner;
    size_t parent_data_size;
  };

  ConnectedSubchannel(
      grpc_channel_stack* channel_stack, const grpc_channel_args* args,
      RefCountedPtr<channelz::SubchannelNode> channelz_subchannel,
      intptr_t socket_uuid);
  ~ConnectedSubchannel();

  void NotifyOnStateChange(grpc_pollset_set* interested_parties,
                           grpc_connectivity_state* state,
                           grpc_closure* closure);
  void Ping(grpc_closure* on_initiate, grpc_closure* on_ack);
  RefCountedPtr<SubchannelCall> CreateCall(const CallArgs& args,
                                           grpc_error** error);

  grpc_channel_stack* channel_stack() const { return channel_stack_; }
  const grpc_channel_args* args() const override { return args_; }
  channelz::SubchannelNode* channelz_subchannel() const {
    return channelz_subchannel_.get();
  }
  intptr_t socket_uuid() const { return socket_uuid_; }

  size_t GetInitialCallSizeEstimate(size_t parent_data_size) const;

 private:
  grpc_channel_stack* channel_stack_;
  grpc_channel_args* args_;
  // ref counted pointer to the channelz node in this connected subchannel's
  // owning subchannel.
  RefCountedPtr<channelz::SubchannelNode> channelz_subchannel_;
  // uuid of this subchannel's socket. 0 if this subchannel is not connected.
  const intptr_t socket_uuid_;
};

// Implements the interface of RefCounted<>.
class SubchannelCall {
 public:
  SubchannelCall(RefCountedPtr<ConnectedSubchannel> connected_subchannel,
                 const ConnectedSubchannel::CallArgs& args)
      : connected_subchannel_(std::move(connected_subchannel)),
        deadline_(args.deadline) {}

  // Continues processing a transport stream op batch.
  void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

  // Returns a pointer to the parent data associated with the subchannel call.
  // The data will be of the size specified in \a parent_data_size field of
  // the args passed to \a ConnectedSubchannel::CreateCall().
  void* GetParentData();

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

  static void Destroy(void* arg, grpc_error* error);

 private:
  // Allow RefCountedPtr<> to access IncrementRefCount().
  template <typename T>
  friend class RefCountedPtr;

  // If channelz is enabled, intercepts recv_trailing so that we may check the
  // status and associate it to a subchannel.
  void MaybeInterceptRecvTrailingMetadata(
      grpc_transport_stream_op_batch* batch);

  static void RecvTrailingMetadataReady(void* arg, grpc_error* error);

  // Interface of RefCounted<>.
  void IncrementRefCount();
  void IncrementRefCount(const DebugLocation& location, const char* reason);

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
class Subchannel {
 public:
  typedef SubchannelInterface::ConnectivityStateWatcher
      ConnectivityStateWatcher;

  // The ctor and dtor are not intended to use directly.
  Subchannel(SubchannelKey* key, grpc_connector* connector,
             const grpc_channel_args* args);
  ~Subchannel();

  // Creates a subchannel given \a connector and \a args.
  static Subchannel* Create(grpc_connector* connector,
                            const grpc_channel_args* args);

  // Strong and weak refcounting.
  Subchannel* Ref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  void Unref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  Subchannel* WeakRef(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  void WeakUnref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
  // Attempts to return a strong ref when only the weak refcount is guaranteed
  // non-zero. If the strong refcount is zero, does not alter the refcount and
  // returns null.
  Subchannel* RefFromWeakRef(GRPC_SUBCHANNEL_REF_EXTRA_ARGS);

  intptr_t GetChildSocketUuid();

  // Gets the string representing the subchannel address.
  // Caller doesn't take ownership.
  const char* GetTargetAddress();

  channelz::SubchannelNode* channelz_node();

  // Returns the current connectivity state of the subchannel.
  // If health_check_service_name is non-null, the returned connectivity
  // state will be based on the state reported by the backend for that
  // service name.
  // If the return value is GRPC_CHANNEL_READY, also sets *connected_subchannel.
  grpc_connectivity_state CheckConnectivityState(
      const char* health_check_service_name,
      RefCountedPtr<ConnectedSubchannel>* connected_subchannel);

  // Starts watching the subchannel's connectivity state.
  // The first callback to the watcher will be delivered when the
  // subchannel's connectivity state becomes a value other than
  // initial_state, which may happen immediately.
  // Subsequent callbacks will be delivered as the subchannel's state
  // changes.
  // The watcher will be destroyed either when the subchannel is
  // destroyed or when CancelConnectivityStateWatch() is called.
  void WatchConnectivityState(grpc_connectivity_state initial_state,
                              UniquePtr<char> health_check_service_name,
                              UniquePtr<ConnectivityStateWatcher> watcher);

  // Cancels a connectivity state watch.
  // If the watcher has already been destroyed, this is a no-op.
  void CancelConnectivityStateWatch(const char* health_check_service_name,
                                    ConnectivityStateWatcher* watcher);

  // Attempt to connect to the backend.  Has no effect if already connected.
  void AttemptToConnect();

  // Resets the connection backoff of the subchannel.
  // TODO(roth): Move connection backoff out of subchannels and up into LB
  // policy code (probably by adding a SubchannelGroup between
  // SubchannelList and SubchannelData), at which point this method can
  // go away.
  void ResetBackoff();

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
  // A linked list of ConnectivityStateWatchers that are monitoring the
  // subchannel's state.
  class ConnectivityStateWatcherList {
   public:
    ~ConnectivityStateWatcherList() { Clear(); }

    void AddWatcherLocked(UniquePtr<ConnectivityStateWatcher> watcher);
    void RemoveWatcherLocked(ConnectivityStateWatcher* watcher);

    // Notifies all watchers in the list about a change to state.
    void NotifyLocked(Subchannel* subchannel, grpc_connectivity_state state);

    void Clear() { watchers_.clear(); }

    bool empty() const { return watchers_.empty(); }

   private:
    // TODO(roth): This could be a set instead of a map if we had a set
    // implementation.
    Map<ConnectivityStateWatcher*, UniquePtr<ConnectivityStateWatcher>>
        watchers_;
  };

  // A map that tracks ConnectivityStateWatchers using a particular health
  // check service name.
  //
  // There is one entry in the map for each health check service name.
  // Entries exist only as long as there are watchers using the
  // corresponding service name.
  //
  // A health check client is maintained only while the subchannel is in
  // state READY.
  class HealthWatcherMap {
   public:
    void AddWatcherLocked(Subchannel* subchannel,
                          grpc_connectivity_state initial_state,
                          UniquePtr<char> health_check_service_name,
                          UniquePtr<ConnectivityStateWatcher> watcher);
    void RemoveWatcherLocked(const char* health_check_service_name,
                             ConnectivityStateWatcher* watcher);

    // Notifies the watcher when the subchannel's state changes.
    void NotifyLocked(grpc_connectivity_state state);

    grpc_connectivity_state CheckConnectivityStateLocked(
        Subchannel* subchannel, const char* health_check_service_name);

    void ShutdownLocked();

   private:
    class HealthWatcher;

    Map<const char*, OrphanablePtr<HealthWatcher>, StringLess> map_;
  };

  class ConnectedSubchannelStateWatcher;

  // Sets the subchannel's connectivity state to \a state.
  void SetConnectivityStateLocked(grpc_connectivity_state state);

  // Methods for connection.
  void MaybeStartConnectingLocked();
  static void OnRetryAlarm(void* arg, grpc_error* error);
  void ContinueConnectingLocked();
  static void OnConnectingFinished(void* arg, grpc_error* error);
  bool PublishTransportLocked();
  void Disconnect();

  gpr_atm RefMutate(gpr_atm delta,
                    int barrier GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS);

  // The subchannel pool this subchannel is in.
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_;
  // TODO(juanlishen): Consider using args_ as key_ directly.
  // Subchannel key that identifies this subchannel in the subchannel pool.
  SubchannelKey* key_;
  // Channel args.
  grpc_channel_args* args_;
  // pollset_set tracking who's interested in a connection being setup.
  grpc_pollset_set* pollset_set_;
  // Protects the other members.
  Mutex mu_;
  // Refcount
  //    - lower INTERNAL_REF_BITS bits are for internal references:
  //      these do not keep the subchannel open.
  //    - upper remaining bits are for public references: these do
  //      keep the subchannel open
  gpr_atm ref_pair_;

  // Connection states.
  grpc_connector* connector_ = nullptr;
  // Set during connection.
  grpc_connect_out_args connecting_result_;
  grpc_closure on_connecting_finished_;
  // Active connection, or null.
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
  bool connecting_ = false;
  bool disconnected_ = false;

  // Connectivity state tracking.
  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  // The list of watchers without a health check service name.
  ConnectivityStateWatcherList watcher_list_;
  // The map of watchers with health check service names.
  HealthWatcherMap health_watcher_map_;

  // Backoff state.
  BackOff backoff_;
  grpc_millis next_attempt_deadline_;
  grpc_millis min_connect_timeout_ms_;
  bool backoff_begun_ = false;

  // Retry alarm.
  grpc_timer retry_alarm_;
  grpc_closure on_retry_alarm_;
  bool have_retry_alarm_ = false;
  // reset_backoff() was called while alarm was pending.
  bool retry_immediately_ = false;

  // Channelz tracking.
  RefCountedPtr<channelz::SubchannelNode> channelz_node_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_H */
