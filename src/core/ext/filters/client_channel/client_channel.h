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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/dynamic_filters.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

//
// Client channel filter
//

// A client channel is a channel that begins disconnected, and can connect
// to some endpoint on demand. If that endpoint disconnects, it will be
// connected to again later.
//
// Calls on a disconnected client channel are queued until a connection is
// established.

// Channel arg key for server URI string.
#define GRPC_ARG_SERVER_URI "grpc.server_uri"

// Channel arg containing a pointer to the ClientChannel object.
#define GRPC_ARG_CLIENT_CHANNEL "grpc.internal.client_channel"

// Channel arg containing a pointer to the ServiceConfig object.
#define GRPC_ARG_SERVICE_CONFIG_OBJ "grpc.internal.service_config_obj"

// Max number of batches that can be pending on a call at any given
// time.  This includes one batch for each of the following ops:
//   recv_initial_metadata
//   send_initial_metadata
//   recv_message
//   send_message
//   recv_trailing_metadata
//   send_trailing_metadata
#define MAX_PENDING_BATCHES 6

namespace grpc_core {

class ClientChannel {
 public:
  static const grpc_channel_filter kFilterVtable;

  class LoadBalancedCall;

  // Returns the ClientChannel object from channel, or null if channel
  // is not a client channel.
  static ClientChannel* GetFromChannel(grpc_channel* channel);

  grpc_connectivity_state CheckConnectivityState(bool try_to_connect);

  // Starts a one-time connectivity state watch.  When the channel's state
  // becomes different from *state, sets *state to the new state and
  // schedules on_complete.  The watcher_timer_init callback is invoked as
  // soon as the watch is actually started (i.e., after hopping into the
  // client channel combiner).  I/O will be serviced via pollent.
  //
  // This is intended to be used when starting a watch from outside of C-core
  // via grpc_channel_watch_connectivity_state().  It should not be used
  // by other callers.
  void AddExternalConnectivityWatcher(grpc_polling_entity pollent,
                                      grpc_connectivity_state* state,
                                      grpc_closure* on_complete,
                                      grpc_closure* watcher_timer_init) {
    new ExternalConnectivityWatcher(this, pollent, state, on_complete,
                                    watcher_timer_init);
  }

  // Cancels a pending external watcher previously added by
  // AddExternalConnectivityWatcher().
  void CancelExternalConnectivityWatcher(grpc_closure* on_complete) {
    ExternalConnectivityWatcher::RemoveWatcherFromExternalWatchersMap(
        this, on_complete, /*cancel=*/true);
  }

  int NumExternalConnectivityWatchers() const {
    MutexLock lock(&external_watchers_mu_);
    return static_cast<int>(external_watchers_.size());
  }

  // Starts and stops a connectivity watch.  The watcher will be initially
  // notified as soon as the state changes from initial_state and then on
  // every subsequent state change until either the watch is stopped or
  // it is notified that the state has changed to SHUTDOWN.
  //
  // This is intended to be used when starting watches from code inside of
  // C-core (e.g., for a nested control plane channel for things like xds).
  void AddConnectivityWatcher(
      grpc_connectivity_state initial_state,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher);
  void RemoveConnectivityWatcher(
      AsyncConnectivityStateWatcherInterface* watcher);

  RefCountedPtr<LoadBalancedCall> CreateLoadBalancedCall(
      const grpc_call_element_args& args, grpc_polling_entity* pollent,
      grpc_closure* on_call_destruction_complete);

 private:
  class CallData;
  class ResolverResultHandler;
  class SubchannelWrapper;
  class ClientChannelControlHelper;
  class ConnectivityWatcherAdder;
  class ConnectivityWatcherRemover;

  // Represents a pending connectivity callback from an external caller
  // via grpc_client_channel_watch_connectivity_state().
  class ExternalConnectivityWatcher : public ConnectivityStateWatcherInterface {
   public:
    ExternalConnectivityWatcher(ClientChannel* chand,
                                grpc_polling_entity pollent,
                                grpc_connectivity_state* state,
                                grpc_closure* on_complete,
                                grpc_closure* watcher_timer_init);

    ~ExternalConnectivityWatcher() override;

    // Removes the watcher from the external_watchers_ map.
    static void RemoveWatcherFromExternalWatchersMap(ClientChannel* chand,
                                                     grpc_closure* on_complete,
                                                     bool cancel);

    void Notify(grpc_connectivity_state state,
                const absl::Status& /* status */) override;

    void Cancel();

   private:
    // Adds the watcher to state_tracker_. Consumes the ref that is passed to it
    // from Start().
    void AddWatcherLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_);
    void RemoveWatcherLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_);

    ClientChannel* chand_;
    grpc_polling_entity pollent_;
    grpc_connectivity_state initial_state_;
    grpc_connectivity_state* state_;
    grpc_closure* on_complete_;
    grpc_closure* watcher_timer_init_;
    Atomic<bool> done_{false};
  };

  struct ResolverQueuedCall {
    grpc_call_element* elem;
    ResolverQueuedCall* next = nullptr;
  };
  struct LbQueuedCall {
    LoadBalancedCall* lb_call;
    LbQueuedCall* next = nullptr;
  };

  ClientChannel(grpc_channel_element_args* args, grpc_error_handle* error);
  ~ClientChannel();

  // Filter vtable functions.
  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);
  static void StartTransportOp(grpc_channel_element* elem,
                               grpc_transport_op* op);
  static void GetChannelInfo(grpc_channel_element* elem,
                             const grpc_channel_info* info);

  // Note: Does NOT return a new ref.
  grpc_error_handle disconnect_error() const {
    return disconnect_error_.Load(MemoryOrder::ACQUIRE);
  }

  // Note: All methods with "Locked" suffix must be invoked from within
  // work_serializer_.

  void OnResolverResultChangedLocked(Resolver::Result result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void OnResolverErrorLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  void CreateOrUpdateLbPolicyLocked(
      RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config,
      Resolver::Result result) ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  OrphanablePtr<LoadBalancingPolicy> CreateLbPolicyLocked(
      const grpc_channel_args& args)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  void UpdateStateAndPickerLocked(
      grpc_connectivity_state state, const absl::Status& status,
      const char* reason,
      std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  void UpdateServiceConfigInControlPlaneLocked(
      RefCountedPtr<ServiceConfig> service_config,
      RefCountedPtr<ConfigSelector> config_selector,
      const internal::ClientChannelGlobalParsedConfig* parsed_service_config,
      const char* lb_policy_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  void UpdateServiceConfigInDataPlaneLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  void CreateResolverLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);
  void DestroyResolverAndLbPolicyLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  grpc_error_handle DoPingLocked(grpc_transport_op* op)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  void StartTransportOpLocked(grpc_transport_op* op)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  void TryToConnectLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(work_serializer_);

  // These methods all require holding resolution_mu_.
  void AddResolverQueuedCall(ResolverQueuedCall* call,
                             grpc_polling_entity* pollent)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(resolution_mu_);
  void RemoveResolverQueuedCall(ResolverQueuedCall* to_remove,
                                grpc_polling_entity* pollent)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(resolution_mu_);

  // These methods all require holding data_plane_mu_.
  void AddLbQueuedCall(LbQueuedCall* call, grpc_polling_entity* pollent)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_plane_mu_);
  void RemoveLbQueuedCall(LbQueuedCall* to_remove, grpc_polling_entity* pollent)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_plane_mu_);
  RefCountedPtr<ConnectedSubchannel> GetConnectedSubchannelInDataPlane(
      SubchannelInterface* subchannel) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_plane_mu_);

  //
  // Fields set at construction and never modified.
  //
  const bool deadline_checking_enabled_;
  const bool enable_retries_;
  grpc_channel_stack* owning_stack_;
  ClientChannelFactory* client_channel_factory_;
  const grpc_channel_args* channel_args_;
  RefCountedPtr<ServiceConfig> default_service_config_;
  std::string server_name_;
  UniquePtr<char> target_uri_;
  channelz::ChannelNode* channelz_node_;
  grpc_pollset_set* interested_parties_;

  //
  // Fields related to name resolution.  Guarded by resolution_mu_.
  //
  mutable Mutex resolution_mu_;
  // Linked list of calls queued waiting for resolver result.
  ResolverQueuedCall* resolver_queued_calls_ ABSL_GUARDED_BY(resolution_mu_) =
      nullptr;
  // Data from service config.
  grpc_error_handle resolver_transient_failure_error_
      ABSL_GUARDED_BY(resolution_mu_) = GRPC_ERROR_NONE;
  bool received_service_config_data_ ABSL_GUARDED_BY(resolution_mu_) = false;
  RefCountedPtr<ServiceConfig> service_config_ ABSL_GUARDED_BY(resolution_mu_);
  RefCountedPtr<ConfigSelector> config_selector_
      ABSL_GUARDED_BY(resolution_mu_);
  RefCountedPtr<DynamicFilters> dynamic_filters_
      ABSL_GUARDED_BY(resolution_mu_);

  //
  // Fields used in the data plane.  Guarded by data_plane_mu_.
  //
  mutable Mutex data_plane_mu_;
  std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker_
      ABSL_GUARDED_BY(data_plane_mu_);
  // Linked list of calls queued waiting for LB pick.
  LbQueuedCall* lb_queued_calls_ ABSL_GUARDED_BY(data_plane_mu_) = nullptr;

  //
  // Fields used in the control plane.  Guarded by work_serializer.
  //
  std::shared_ptr<WorkSerializer> work_serializer_;
  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(work_serializer_);
  OrphanablePtr<Resolver> resolver_ ABSL_GUARDED_BY(work_serializer_);
  bool previous_resolution_contained_addresses_
      ABSL_GUARDED_BY(work_serializer_) = false;
  RefCountedPtr<ServiceConfig> saved_service_config_
      ABSL_GUARDED_BY(work_serializer_);
  RefCountedPtr<ConfigSelector> saved_config_selector_
      ABSL_GUARDED_BY(work_serializer_);
  absl::optional<std::string> health_check_service_name_
      ABSL_GUARDED_BY(work_serializer_);
  OrphanablePtr<LoadBalancingPolicy> lb_policy_
      ABSL_GUARDED_BY(work_serializer_);
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_
      ABSL_GUARDED_BY(work_serializer_);
  // The number of SubchannelWrapper instances referencing a given Subchannel.
  std::map<Subchannel*, int> subchannel_refcount_map_
      ABSL_GUARDED_BY(work_serializer_);
  // The set of SubchannelWrappers that currently exist.
  // No need to hold a ref, since the map is updated in the control-plane
  // work_serializer when the SubchannelWrappers are created and destroyed.
  std::set<SubchannelWrapper*> subchannel_wrappers_
      ABSL_GUARDED_BY(work_serializer_);
  // Pending ConnectedSubchannel updates for each SubchannelWrapper.
  // Updates are queued here in the control plane work_serializer and then
  // applied in the data plane mutex when the picker is updated.
  std::map<RefCountedPtr<SubchannelWrapper>, RefCountedPtr<ConnectedSubchannel>>
      pending_subchannel_updates_ ABSL_GUARDED_BY(work_serializer_);
  int keepalive_time_ ABSL_GUARDED_BY(work_serializer_) = -1;

  //
  // Fields accessed from both data plane mutex and control plane
  // work_serializer.
  //
  Atomic<grpc_error_handle> disconnect_error_;

  //
  // Fields guarded by a mutex, since they need to be accessed
  // synchronously via get_channel_info().
  //
  Mutex info_mu_;
  UniquePtr<char> info_lb_policy_name_ ABSL_GUARDED_BY(info_mu_);
  UniquePtr<char> info_service_config_json_ ABSL_GUARDED_BY(info_mu_);

  //
  // Fields guarded by a mutex, since they need to be accessed
  // synchronously via grpc_channel_num_external_connectivity_watchers().
  //
  mutable Mutex external_watchers_mu_;
  std::map<grpc_closure*, RefCountedPtr<ExternalConnectivityWatcher>>
      external_watchers_ ABSL_GUARDED_BY(external_watchers_mu_);
};

//
// ClientChannel::LoadBalancedCall
//

// This object is ref-counted, but it cannot inherit from RefCounted<>,
// because it is allocated on the arena and can't free its memory when
// its refcount goes to zero.  So instead, it manually implements the
// same API as RefCounted<>, so that it can be used with RefCountedPtr<>.
class ClientChannel::LoadBalancedCall
    : public RefCounted<LoadBalancedCall, PolymorphicRefCount, kUnrefCallDtor> {
 public:
  // If on_call_destruction_complete is non-null, then it will be
  // invoked once the LoadBalancedCall is completely destroyed.
  // If it is null, then the caller is responsible for checking whether
  // the LB call has a subchannel call and ensuring that the
  // on_call_destruction_complete closure passed down from the surface
  // is not invoked until after the subchannel call stack is destroyed.
  LoadBalancedCall(ClientChannel* chand, const grpc_call_element_args& args,
                   grpc_polling_entity* pollent,
                   grpc_closure* on_call_destruction_complete);
  ~LoadBalancedCall() override;

  void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch);

  // Invoked by channel for queued LB picks when the picker is updated.
  static void PickSubchannel(void* arg, grpc_error_handle error);
  // Helper function for performing an LB pick while holding the data plane
  // mutex.  Returns true if the pick is complete, in which case the caller
  // must invoke PickDone() or AsyncPickDone() with the returned error.
  bool PickSubchannelLocked(grpc_error_handle* error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::data_plane_mu_);
  // Schedules a callback to process the completed pick.  The callback
  // will not run until after this method returns.
  void AsyncPickDone(grpc_error_handle error);

  RefCountedPtr<SubchannelCall> subchannel_call() const {
    return subchannel_call_;
  }

 private:
  class LbQueuedCallCanceller;
  class Metadata;
  class LbCallState;

  // Returns the index into pending_batches_ to be used for batch.
  static size_t GetBatchIndex(grpc_transport_stream_op_batch* batch);
  void PendingBatchesAdd(grpc_transport_stream_op_batch* batch);
  static void FailPendingBatchInCallCombiner(void* arg,
                                             grpc_error_handle error);
  // A predicate type and some useful implementations for PendingBatchesFail().
  typedef bool (*YieldCallCombinerPredicate)(
      const CallCombinerClosureList& closures);
  static bool YieldCallCombiner(const CallCombinerClosureList& /*closures*/) {
    return true;
  }
  static bool NoYieldCallCombiner(const CallCombinerClosureList& /*closures*/) {
    return false;
  }
  static bool YieldCallCombinerIfPendingBatchesFound(
      const CallCombinerClosureList& closures) {
    return closures.size() > 0;
  }
  // Fails all pending batches.
  // If yield_call_combiner_predicate returns true, assumes responsibility for
  // yielding the call combiner.
  void PendingBatchesFail(
      grpc_error_handle error,
      YieldCallCombinerPredicate yield_call_combiner_predicate);
  static void ResumePendingBatchInCallCombiner(void* arg,
                                               grpc_error_handle ignored);
  // Resumes all pending batches on subchannel_call_.
  void PendingBatchesResume();

  static void RecvTrailingMetadataReadyForLoadBalancingPolicy(
      void* arg, grpc_error_handle error);
  void InjectRecvTrailingMetadataReadyForLoadBalancingPolicy(
      grpc_transport_stream_op_batch* batch);

  void CreateSubchannelCall();
  // Invoked when a pick is completed, on both success or failure.
  static void PickDone(void* arg, grpc_error_handle error);
  // Removes the call from the channel's list of queued picks if present.
  void MaybeRemoveCallFromLbQueuedCallsLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::data_plane_mu_);
  // Adds the call to the channel's list of queued picks if not already present.
  void MaybeAddCallToLbQueuedCallsLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::data_plane_mu_);

  ClientChannel* chand_;

  // TODO(roth): Instead of duplicating these fields in every filter
  // that uses any one of them, we should store them in the call
  // context.  This will save per-call memory overhead.
  grpc_slice path_;  // Request path.
  gpr_cycle_counter call_start_time_;
  grpc_millis deadline_;
  Arena* arena_;
  grpc_call_stack* owning_call_;
  CallCombiner* call_combiner_;
  grpc_call_context_element* call_context_;
  grpc_polling_entity* pollent_;
  grpc_closure* on_call_destruction_complete_;

  // Set when we get a cancel_stream op.
  grpc_error_handle cancel_error_ = GRPC_ERROR_NONE;

  // Set when we fail inside the LB call.
  grpc_error_handle failure_error_ = GRPC_ERROR_NONE;

  grpc_closure pick_closure_;

  // Accessed while holding ClientChannel::data_plane_mu_.
  ClientChannel::LbQueuedCall queued_call_
      ABSL_GUARDED_BY(&ClientChannel::data_plane_mu_);
  bool queued_pending_lb_pick_ ABSL_GUARDED_BY(&ClientChannel::data_plane_mu_) =
      false;
  LbQueuedCallCanceller* lb_call_canceller_
      ABSL_GUARDED_BY(&ClientChannel::data_plane_mu_) = nullptr;

  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
  const LoadBalancingPolicy::BackendMetricData* backend_metric_data_ = nullptr;
  std::function<void(grpc_error_handle, LoadBalancingPolicy::MetadataInterface*,
                     LoadBalancingPolicy::CallState*)>
      lb_recv_trailing_metadata_ready_;

  RefCountedPtr<SubchannelCall> subchannel_call_;

  // For intercepting recv_trailing_metadata_ready for the LB policy.
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the subchannel call and are not
  // intercepting any of its callbacks).
  grpc_transport_stream_op_batch* pending_batches_[MAX_PENDING_BATCHES] = {};
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_H
