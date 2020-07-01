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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/client_channel.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <set>

#include "absl/strings/string_view.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "absl/container/inlined_vector.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/backend_metric.h"
#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/resolving_lb_policy.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/service_config_call_data.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

using grpc_core::internal::ClientChannelMethodParsedConfig;
using grpc_core::internal::ServerRetryThrottleData;

//
// Client channel filter
//

// By default, we buffer 256 KiB per RPC for retries.
// TODO(roth): Do we have any data to suggest a better value?
#define DEFAULT_PER_RPC_RETRY_BUFFER_SIZE (256 << 10)

// This value was picked arbitrarily.  It can be changed if there is
// any even moderately compelling reason to do so.
#define RETRY_BACKOFF_JITTER 0.2

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

TraceFlag grpc_client_channel_call_trace(false, "client_channel_call");
TraceFlag grpc_client_channel_routing_trace(false, "client_channel_routing");

namespace {

//
// ChannelData definition
//

class ChannelData {
 public:
  struct QueuedPick {
    grpc_call_element* elem;
    QueuedPick* next = nullptr;
  };

  static grpc_error* Init(grpc_channel_element* elem,
                          grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);
  static void StartTransportOp(grpc_channel_element* elem,
                               grpc_transport_op* op);
  static void GetChannelInfo(grpc_channel_element* elem,
                             const grpc_channel_info* info);

  bool deadline_checking_enabled() const { return deadline_checking_enabled_; }
  bool enable_retries() const { return enable_retries_; }
  size_t per_rpc_retry_buffer_size() const {
    return per_rpc_retry_buffer_size_;
  }
  grpc_channel_stack* owning_stack() const { return owning_stack_; }

  // Note: Does NOT return a new ref.
  grpc_error* disconnect_error() const {
    return disconnect_error_.Load(MemoryOrder::ACQUIRE);
  }

  Mutex* data_plane_mu() const { return &data_plane_mu_; }

  LoadBalancingPolicy::SubchannelPicker* picker() const {
    return picker_.get();
  }
  void AddQueuedPick(QueuedPick* pick, grpc_polling_entity* pollent);
  void RemoveQueuedPick(QueuedPick* to_remove, grpc_polling_entity* pollent);

  bool received_service_config_data() const {
    return received_service_config_data_;
  }
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data() const {
    return retry_throttle_data_;
  }
  RefCountedPtr<ServiceConfig> service_config() const {
    return service_config_;
  }
  WorkSerializer* work_serializer() const { return work_serializer_.get(); }

  RefCountedPtr<ConnectedSubchannel> GetConnectedSubchannelInDataPlane(
      SubchannelInterface* subchannel) const;

  grpc_connectivity_state CheckConnectivityState(bool try_to_connect);

  void AddExternalConnectivityWatcher(grpc_polling_entity pollent,
                                      grpc_connectivity_state* state,
                                      grpc_closure* on_complete,
                                      grpc_closure* watcher_timer_init) {
    auto* watcher = new ExternalConnectivityWatcher(
        this, pollent, state, on_complete, watcher_timer_init);
    {
      MutexLock lock(&external_watchers_mu_);
      // Will be deleted when the watch is complete.
      GPR_ASSERT(external_watchers_[on_complete] == nullptr);
      external_watchers_[on_complete] = watcher;
    }
    watcher->Start();
  }

  void RemoveExternalConnectivityWatcher(grpc_closure* on_complete,
                                         bool cancel) {
    ExternalConnectivityWatcher* watcher = nullptr;
    {
      MutexLock lock(&external_watchers_mu_);
      auto it = external_watchers_.find(on_complete);
      if (it != external_watchers_.end()) {
        watcher = it->second;
        external_watchers_.erase(it);
      }
    }
    if (watcher != nullptr && cancel) watcher->Cancel();
  }

  int NumExternalConnectivityWatchers() const {
    MutexLock lock(&external_watchers_mu_);
    return static_cast<int>(external_watchers_.size());
  }

  void AddConnectivityWatcher(
      grpc_connectivity_state initial_state,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher);
  void RemoveConnectivityWatcher(
      AsyncConnectivityStateWatcherInterface* watcher);

 private:
  class SubchannelWrapper;
  class ClientChannelControlHelper;
  class ConnectivityWatcherAdder;
  class ConnectivityWatcherRemover;

  // Represents a pending connectivity callback from an external caller
  // via grpc_client_channel_watch_connectivity_state().
  class ExternalConnectivityWatcher : public ConnectivityStateWatcherInterface {
   public:
    ExternalConnectivityWatcher(ChannelData* chand, grpc_polling_entity pollent,
                                grpc_connectivity_state* state,
                                grpc_closure* on_complete,
                                grpc_closure* watcher_timer_init);

    ~ExternalConnectivityWatcher();

    void Start();

    void Notify(grpc_connectivity_state state) override;

    void Cancel();

   private:
    void AddWatcherLocked();
    void RemoveWatcherLocked();

    ChannelData* chand_;
    grpc_polling_entity pollent_;
    grpc_connectivity_state initial_state_;
    grpc_connectivity_state* state_;
    grpc_closure* on_complete_;
    grpc_closure* watcher_timer_init_;
    Atomic<bool> done_{false};
  };

  ChannelData(grpc_channel_element_args* args, grpc_error** error);
  ~ChannelData();

  void UpdateStateAndPickerLocked(
      grpc_connectivity_state state, const char* reason,
      std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker);

  void UpdateServiceConfigLocked(
      RefCountedPtr<ServerRetryThrottleData> retry_throttle_data,
      RefCountedPtr<ServiceConfig> service_config);

  void CreateResolvingLoadBalancingPolicyLocked();

  void DestroyResolvingLoadBalancingPolicyLocked();

  static bool ProcessResolverResultLocked(
      void* arg, const Resolver::Result& result,
      RefCountedPtr<LoadBalancingPolicy::Config>* lb_policy_config,
      grpc_error** service_config_error, bool* no_valid_service_config);

  grpc_error* DoPingLocked(grpc_transport_op* op);

  void StartTransportOpLocked(grpc_transport_op* op);

  void TryToConnectLocked();

  void ProcessLbPolicy(
      const Resolver::Result& resolver_result,
      const internal::ClientChannelGlobalParsedConfig* parsed_service_config,
      RefCountedPtr<LoadBalancingPolicy::Config>* lb_policy_config);

  //
  // Fields set at construction and never modified.
  //
  const bool deadline_checking_enabled_;
  const bool enable_retries_;
  const size_t per_rpc_retry_buffer_size_;
  grpc_channel_stack* owning_stack_;
  ClientChannelFactory* client_channel_factory_;
  const grpc_channel_args* channel_args_;
  RefCountedPtr<ServiceConfig> default_service_config_;
  grpc_core::UniquePtr<char> server_name_;
  grpc_core::UniquePtr<char> target_uri_;
  channelz::ChannelNode* channelz_node_;

  //
  // Fields used in the data plane.  Guarded by data_plane_mu.
  //
  mutable Mutex data_plane_mu_;
  std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker_;
  QueuedPick* queued_picks_ = nullptr;  // Linked list of queued picks.
  // Data from service config.
  bool received_service_config_data_ = false;
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_;
  RefCountedPtr<ServiceConfig> service_config_;

  //
  // Fields used in the control plane.  Guarded by work_serializer.
  //
  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_pollset_set* interested_parties_;
  RefCountedPtr<SubchannelPoolInterface> subchannel_pool_;
  OrphanablePtr<ResolvingLoadBalancingPolicy> resolving_lb_policy_;
  ConnectivityStateTracker state_tracker_;
  grpc_core::UniquePtr<char> health_check_service_name_;
  RefCountedPtr<ServiceConfig> saved_service_config_;
  bool received_first_resolver_result_ = false;
  // The number of SubchannelWrapper instances referencing a given Subchannel.
  std::map<Subchannel*, int> subchannel_refcount_map_;
  // The set of SubchannelWrappers that currently exist.
  // No need to hold a ref, since the map is updated in the control-plane
  // work_serializer when the SubchannelWrappers are created and destroyed.
  std::set<SubchannelWrapper*> subchannel_wrappers_;
  // Pending ConnectedSubchannel updates for each SubchannelWrapper.
  // Updates are queued here in the control plane work_serializer and then
  // applied in the data plane mutex when the picker is updated.
  std::map<RefCountedPtr<SubchannelWrapper>, RefCountedPtr<ConnectedSubchannel>>
      pending_subchannel_updates_;

  //
  // Fields accessed from both data plane mutex and control plane
  // work_serializer.
  //
  Atomic<grpc_error*> disconnect_error_;

  //
  // Fields guarded by a mutex, since they need to be accessed
  // synchronously via get_channel_info().
  //
  gpr_mu info_mu_;
  grpc_core::UniquePtr<char> info_lb_policy_name_;
  grpc_core::UniquePtr<char> info_service_config_json_;

  //
  // Fields guarded by a mutex, since they need to be accessed
  // synchronously via grpc_channel_num_external_connectivity_watchers().
  //
  mutable Mutex external_watchers_mu_;
  std::map<grpc_closure*, ExternalConnectivityWatcher*> external_watchers_;
};

//
// CallData definition
//

class CallData {
 public:
  static grpc_error* Init(grpc_call_element* elem,
                          const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* final_info,
                      grpc_closure* then_schedule_closure);
  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
  static void SetPollent(grpc_call_element* elem, grpc_polling_entity* pollent);

  RefCountedPtr<SubchannelCall> subchannel_call() { return subchannel_call_; }

  // Invoked by channel for queued picks once resolver results are available.
  void MaybeApplyServiceConfigToCallLocked(grpc_call_element* elem);

  // Invoked by channel for queued picks when the picker is updated.
  static void PickSubchannel(void* arg, grpc_error* error);

  // Helper function for performing a pick while holding the data plane
  // mutex.  Returns true if the pick is complete, in which case the caller
  // must invoke PickDone() or AsyncPickDone() with the returned error.
  bool PickSubchannelLocked(grpc_call_element* elem, grpc_error** error);

  // Schedules a callback to process the completed pick.  The callback
  // will not run until after this method returns.
  void AsyncPickDone(grpc_call_element* elem, grpc_error* error);

 private:
  class QueuedPickCanceller;

  class Metadata : public LoadBalancingPolicy::MetadataInterface {
   public:
    Metadata(CallData* calld, grpc_metadata_batch* batch)
        : calld_(calld), batch_(batch) {}

    void Add(absl::string_view key, absl::string_view value) override {
      grpc_linked_mdelem* linked_mdelem = static_cast<grpc_linked_mdelem*>(
          calld_->arena_->Alloc(sizeof(grpc_linked_mdelem)));
      linked_mdelem->md = grpc_mdelem_from_slices(
          grpc_core::ExternallyManagedSlice(key.data(), key.size()),
          grpc_core::ExternallyManagedSlice(value.data(), value.size()));
      GPR_ASSERT(grpc_metadata_batch_link_tail(batch_, linked_mdelem) ==
                 GRPC_ERROR_NONE);
    }

    iterator begin() const override {
      static_assert(sizeof(grpc_linked_mdelem*) <= sizeof(intptr_t),
                    "iterator size too large");
      return iterator(this, reinterpret_cast<intptr_t>(batch_->list.head));
    }
    iterator end() const override {
      static_assert(sizeof(grpc_linked_mdelem*) <= sizeof(intptr_t),
                    "iterator size too large");
      return iterator(this, 0);
    }

    iterator erase(iterator it) override {
      grpc_linked_mdelem* linked_mdelem =
          reinterpret_cast<grpc_linked_mdelem*>(GetIteratorHandle(it));
      intptr_t handle = reinterpret_cast<intptr_t>(linked_mdelem->next);
      grpc_metadata_batch_remove(batch_, linked_mdelem);
      return iterator(this, handle);
    }

   private:
    intptr_t IteratorHandleNext(intptr_t handle) const override {
      grpc_linked_mdelem* linked_mdelem =
          reinterpret_cast<grpc_linked_mdelem*>(handle);
      return reinterpret_cast<intptr_t>(linked_mdelem->next);
    }
    std::pair<absl::string_view, absl::string_view> IteratorHandleGet(
        intptr_t handle) const override {
      grpc_linked_mdelem* linked_mdelem =
          reinterpret_cast<grpc_linked_mdelem*>(handle);
      return std::make_pair(
          StringViewFromSlice(GRPC_MDKEY(linked_mdelem->md)),
          StringViewFromSlice(GRPC_MDVALUE(linked_mdelem->md)));
    }

    CallData* calld_;
    grpc_metadata_batch* batch_;
  };

  class LbCallState : public LoadBalancingPolicy::CallState {
   public:
    explicit LbCallState(CallData* calld) : calld_(calld) {}

    void* Alloc(size_t size) override { return calld_->arena_->Alloc(size); }

    const LoadBalancingPolicy::BackendMetricData* GetBackendMetricData()
        override {
      if (calld_->backend_metric_data_ == nullptr) {
        grpc_linked_mdelem* md = calld_->recv_trailing_metadata_->idx.named
                                     .x_endpoint_load_metrics_bin;
        if (md != nullptr) {
          calld_->backend_metric_data_ =
              ParseBackendMetricData(GRPC_MDVALUE(md->md), calld_->arena_);
        }
      }
      return calld_->backend_metric_data_;
    }

    absl::string_view ExperimentalGetCallAttribute(const char* key) override {
      auto it = calld_->call_attributes_.find(key);
      if (it == calld_->call_attributes_.end()) return absl::string_view();
      return it->second;
    }

   private:
    CallData* calld_;
  };

  // State used for starting a retryable batch on a subchannel call.
  // This provides its own grpc_transport_stream_op_batch and other data
  // structures needed to populate the ops in the batch.
  // We allocate one struct on the arena for each attempt at starting a
  // batch on a given subchannel call.
  struct SubchannelCallBatchData {
    // Creates a SubchannelCallBatchData object on the call's arena with the
    // specified refcount.  If set_on_complete is true, the batch's
    // on_complete callback will be set to point to on_complete();
    // otherwise, the batch's on_complete callback will be null.
    static SubchannelCallBatchData* Create(grpc_call_element* elem,
                                           int refcount, bool set_on_complete);

    void Unref() {
      if (gpr_unref(&refs)) Destroy();
    }

    SubchannelCallBatchData(grpc_call_element* elem, CallData* calld,
                            int refcount, bool set_on_complete);
    // All dtor code must be added in `Destroy()`. This is because we may
    // call closures in `SubchannelCallBatchData` after they are unrefed by
    // `Unref()`, and msan would complain about accessing this class
    // after calling dtor. As a result we cannot call the `dtor` in `Unref()`.
    // TODO(soheil): We should try to call the dtor in `Unref()`.
    ~SubchannelCallBatchData() { Destroy(); }
    void Destroy();

    gpr_refcount refs;
    grpc_call_element* elem;
    RefCountedPtr<SubchannelCall> subchannel_call;
    // The batch to use in the subchannel call.
    // Its payload field points to SubchannelCallRetryState::batch_payload.
    grpc_transport_stream_op_batch batch;
    // For intercepting on_complete.
    grpc_closure on_complete;
  };

  // Retry state associated with a subchannel call.
  // Stored in the parent_data of the subchannel call object.
  struct SubchannelCallRetryState {
    explicit SubchannelCallRetryState(grpc_call_context_element* context)
        : batch_payload(context),
          started_send_initial_metadata(false),
          completed_send_initial_metadata(false),
          started_send_trailing_metadata(false),
          completed_send_trailing_metadata(false),
          started_recv_initial_metadata(false),
          completed_recv_initial_metadata(false),
          started_recv_trailing_metadata(false),
          completed_recv_trailing_metadata(false),
          retry_dispatched(false) {}

    // SubchannelCallBatchData.batch.payload points to this.
    grpc_transport_stream_op_batch_payload batch_payload;
    // For send_initial_metadata.
    // Note that we need to make a copy of the initial metadata for each
    // subchannel call instead of just referring to the copy in call_data,
    // because filters in the subchannel stack will probably add entries,
    // so we need to start in a pristine state for each attempt of the call.
    grpc_linked_mdelem* send_initial_metadata_storage;
    grpc_metadata_batch send_initial_metadata;
    // For send_message.
    // TODO(roth): Restructure this to eliminate use of ManualConstructor.
    ManualConstructor<ByteStreamCache::CachingByteStream> send_message;
    // For send_trailing_metadata.
    grpc_linked_mdelem* send_trailing_metadata_storage;
    grpc_metadata_batch send_trailing_metadata;
    // For intercepting recv_initial_metadata.
    grpc_metadata_batch recv_initial_metadata;
    grpc_closure recv_initial_metadata_ready;
    bool trailing_metadata_available = false;
    // For intercepting recv_message.
    grpc_closure recv_message_ready;
    OrphanablePtr<ByteStream> recv_message;
    // For intercepting recv_trailing_metadata.
    grpc_metadata_batch recv_trailing_metadata;
    grpc_transport_stream_stats collect_stats;
    grpc_closure recv_trailing_metadata_ready;
    // These fields indicate which ops have been started and completed on
    // this subchannel call.
    size_t started_send_message_count = 0;
    size_t completed_send_message_count = 0;
    size_t started_recv_message_count = 0;
    size_t completed_recv_message_count = 0;
    bool started_send_initial_metadata : 1;
    bool completed_send_initial_metadata : 1;
    bool started_send_trailing_metadata : 1;
    bool completed_send_trailing_metadata : 1;
    bool started_recv_initial_metadata : 1;
    bool completed_recv_initial_metadata : 1;
    bool started_recv_trailing_metadata : 1;
    bool completed_recv_trailing_metadata : 1;
    // State for callback processing.
    SubchannelCallBatchData* recv_initial_metadata_ready_deferred_batch =
        nullptr;
    grpc_error* recv_initial_metadata_error = GRPC_ERROR_NONE;
    SubchannelCallBatchData* recv_message_ready_deferred_batch = nullptr;
    grpc_error* recv_message_error = GRPC_ERROR_NONE;
    SubchannelCallBatchData* recv_trailing_metadata_internal_batch = nullptr;
    // NOTE: Do not move this next to the metadata bitfields above. That would
    //       save space but will also result in a data race because compiler
    //       will generate a 2 byte store which overwrites the meta-data
    //       fields upon setting this field.
    bool retry_dispatched : 1;
  };

  // Pending batches stored in call data.
  struct PendingBatch {
    // The pending batch.  If nullptr, this slot is empty.
    grpc_transport_stream_op_batch* batch;
    // Indicates whether payload for send ops has been cached in CallData.
    bool send_ops_cached;
  };

  CallData(grpc_call_element* elem, const ChannelData& chand,
           const grpc_call_element_args& args);
  ~CallData();

  // Caches data for send ops so that it can be retried later, if not
  // already cached.
  void MaybeCacheSendOpsForBatch(PendingBatch* pending);
  void FreeCachedSendInitialMetadata(ChannelData* chand);
  // Frees cached send_message at index idx.
  void FreeCachedSendMessage(ChannelData* chand, size_t idx);
  void FreeCachedSendTrailingMetadata(ChannelData* chand);
  // Frees cached send ops that have already been completed after
  // committing the call.
  void FreeCachedSendOpDataAfterCommit(grpc_call_element* elem,
                                       SubchannelCallRetryState* retry_state);
  // Frees cached send ops that were completed by the completed batch in
  // batch_data.  Used when batches are completed after the call is committed.
  void FreeCachedSendOpDataForCompletedBatch(
      grpc_call_element* elem, SubchannelCallBatchData* batch_data,
      SubchannelCallRetryState* retry_state);

  static void RecvTrailingMetadataReadyForLoadBalancingPolicy(
      void* arg, grpc_error* error);
  void MaybeInjectRecvTrailingMetadataReadyForLoadBalancingPolicy(
      grpc_transport_stream_op_batch* batch);

  // Returns the index into pending_batches_ to be used for batch.
  static size_t GetBatchIndex(grpc_transport_stream_op_batch* batch);
  void PendingBatchesAdd(grpc_call_element* elem,
                         grpc_transport_stream_op_batch* batch);
  void PendingBatchClear(PendingBatch* pending);
  void MaybeClearPendingBatch(grpc_call_element* elem, PendingBatch* pending);
  static void FailPendingBatchInCallCombiner(void* arg, grpc_error* error);
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
      grpc_call_element* elem, grpc_error* error,
      YieldCallCombinerPredicate yield_call_combiner_predicate);
  static void ResumePendingBatchInCallCombiner(void* arg, grpc_error* ignored);
  // Resumes all pending batches on subchannel_call_.
  void PendingBatchesResume(grpc_call_element* elem);
  // Returns a pointer to the first pending batch for which predicate(batch)
  // returns true, or null if not found.
  template <typename Predicate>
  PendingBatch* PendingBatchFind(grpc_call_element* elem,
                                 const char* log_message, Predicate predicate);

  // Commits the call so that no further retry attempts will be performed.
  void RetryCommit(grpc_call_element* elem,
                   SubchannelCallRetryState* retry_state);
  // Starts a retry after appropriate back-off.
  void DoRetry(grpc_call_element* elem, SubchannelCallRetryState* retry_state,
               grpc_millis server_pushback_ms);
  // Returns true if the call is being retried.
  bool MaybeRetry(grpc_call_element* elem, SubchannelCallBatchData* batch_data,
                  grpc_status_code status, grpc_mdelem* server_pushback_md);

  // Invokes recv_initial_metadata_ready for a subchannel batch.
  static void InvokeRecvInitialMetadataCallback(void* arg, grpc_error* error);
  // Intercepts recv_initial_metadata_ready callback for retries.
  // Commits the call and returns the initial metadata up the stack.
  static void RecvInitialMetadataReady(void* arg, grpc_error* error);

  // Invokes recv_message_ready for a subchannel batch.
  static void InvokeRecvMessageCallback(void* arg, grpc_error* error);
  // Intercepts recv_message_ready callback for retries.
  // Commits the call and returns the message up the stack.
  static void RecvMessageReady(void* arg, grpc_error* error);

  // Sets *status and *server_pushback_md based on md_batch and error.
  // Only sets *server_pushback_md if server_pushback_md != nullptr.
  void GetCallStatus(grpc_metadata_batch* md_batch, grpc_error* error,
                     grpc_status_code* status,
                     grpc_mdelem** server_pushback_md);
  // Adds recv_trailing_metadata_ready closure to closures.
  void AddClosureForRecvTrailingMetadataReady(
      grpc_call_element* elem, SubchannelCallBatchData* batch_data,
      grpc_error* error, CallCombinerClosureList* closures);
  // Adds any necessary closures for deferred recv_initial_metadata and
  // recv_message callbacks to closures.
  static void AddClosuresForDeferredRecvCallbacks(
      SubchannelCallBatchData* batch_data,
      SubchannelCallRetryState* retry_state, CallCombinerClosureList* closures);
  // Returns true if any op in the batch was not yet started.
  // Only looks at send ops, since recv ops are always started immediately.
  bool PendingBatchIsUnstarted(PendingBatch* pending,
                               SubchannelCallRetryState* retry_state);
  // For any pending batch containing an op that has not yet been started,
  // adds the pending batch's completion closures to closures.
  void AddClosuresToFailUnstartedPendingBatches(
      grpc_call_element* elem, SubchannelCallRetryState* retry_state,
      grpc_error* error, CallCombinerClosureList* closures);
  // Runs necessary closures upon completion of a call attempt.
  void RunClosuresForCompletedCall(SubchannelCallBatchData* batch_data,
                                   grpc_error* error);
  // Intercepts recv_trailing_metadata_ready callback for retries.
  // Commits the call and returns the trailing metadata up the stack.
  static void RecvTrailingMetadataReady(void* arg, grpc_error* error);

  // Adds the on_complete closure for the pending batch completed in
  // batch_data to closures.
  void AddClosuresForCompletedPendingBatch(grpc_call_element* elem,
                                           SubchannelCallBatchData* batch_data,
                                           grpc_error* error,
                                           CallCombinerClosureList* closures);

  // If there are any cached ops to replay or pending ops to start on the
  // subchannel call, adds a closure to closures to invoke
  // StartRetriableSubchannelBatches().
  void AddClosuresForReplayOrPendingSendOps(
      grpc_call_element* elem, SubchannelCallBatchData* batch_data,
      SubchannelCallRetryState* retry_state, CallCombinerClosureList* closures);

  // Callback used to intercept on_complete from subchannel calls.
  // Called only when retries are enabled.
  static void OnComplete(void* arg, grpc_error* error);

  static void StartBatchInCallCombiner(void* arg, grpc_error* ignored);
  // Adds a closure to closures that will execute batch in the call combiner.
  void AddClosureForSubchannelBatch(grpc_call_element* elem,
                                    grpc_transport_stream_op_batch* batch,
                                    CallCombinerClosureList* closures);
  // Adds retriable send_initial_metadata op to batch_data.
  void AddRetriableSendInitialMetadataOp(SubchannelCallRetryState* retry_state,
                                         SubchannelCallBatchData* batch_data);
  // Adds retriable send_message op to batch_data.
  void AddRetriableSendMessageOp(grpc_call_element* elem,
                                 SubchannelCallRetryState* retry_state,
                                 SubchannelCallBatchData* batch_data);
  // Adds retriable send_trailing_metadata op to batch_data.
  void AddRetriableSendTrailingMetadataOp(SubchannelCallRetryState* retry_state,
                                          SubchannelCallBatchData* batch_data);
  // Adds retriable recv_initial_metadata op to batch_data.
  void AddRetriableRecvInitialMetadataOp(SubchannelCallRetryState* retry_state,
                                         SubchannelCallBatchData* batch_data);
  // Adds retriable recv_message op to batch_data.
  void AddRetriableRecvMessageOp(SubchannelCallRetryState* retry_state,
                                 SubchannelCallBatchData* batch_data);
  // Adds retriable recv_trailing_metadata op to batch_data.
  void AddRetriableRecvTrailingMetadataOp(SubchannelCallRetryState* retry_state,
                                          SubchannelCallBatchData* batch_data);
  // Helper function used to start a recv_trailing_metadata batch.  This
  // is used in the case where a recv_initial_metadata or recv_message
  // op fails in a way that we know the call is over but when the application
  // has not yet started its own recv_trailing_metadata op.
  void StartInternalRecvTrailingMetadata(grpc_call_element* elem);
  // If there are any cached send ops that need to be replayed on the
  // current subchannel call, creates and returns a new subchannel batch
  // to replay those ops.  Otherwise, returns nullptr.
  SubchannelCallBatchData* MaybeCreateSubchannelBatchForReplay(
      grpc_call_element* elem, SubchannelCallRetryState* retry_state);
  // Adds subchannel batches for pending batches to closures.
  void AddSubchannelBatchesForPendingBatches(
      grpc_call_element* elem, SubchannelCallRetryState* retry_state,
      CallCombinerClosureList* closures);
  // Constructs and starts whatever subchannel batches are needed on the
  // subchannel call.
  static void StartRetriableSubchannelBatches(void* arg, grpc_error* ignored);

  void CreateSubchannelCall(grpc_call_element* elem);
  // Invoked when a pick is completed, on both success or failure.
  static void PickDone(void* arg, grpc_error* error);
  // Removes the call from the channel's list of queued picks.
  void RemoveCallFromQueuedPicksLocked(grpc_call_element* elem);
  // Adds the call to the channel's list of queued picks.
  void AddCallToQueuedPicksLocked(grpc_call_element* elem);
  // Applies service config to the call.  Must be invoked once we know
  // that the resolver has returned results to the channel.
  void ApplyServiceConfigToCallLocked(grpc_call_element* elem);

  // State for handling deadlines.
  // The code in deadline_filter.c requires this to be the first field.
  // TODO(roth): This is slightly sub-optimal in that grpc_deadline_state
  // and this struct both independently store pointers to the call stack
  // and call combiner.  If/when we have time, find a way to avoid this
  // without breaking the grpc_deadline_state abstraction.
  grpc_deadline_state deadline_state_;

  grpc_slice path_;  // Request path.
  gpr_cycle_counter call_start_time_;
  grpc_millis deadline_;
  Arena* arena_;
  grpc_call_stack* owning_call_;
  CallCombiner* call_combiner_;
  grpc_call_context_element* call_context_;

  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_;
  const ClientChannelMethodParsedConfig* method_params_ = nullptr;
  std::map<const char*, absl::string_view> call_attributes_;

  RefCountedPtr<SubchannelCall> subchannel_call_;

  // Set when we get a cancel_stream op.
  grpc_error* cancel_error_ = GRPC_ERROR_NONE;

  ChannelData::QueuedPick pick_;
  bool pick_queued_ = false;
  bool service_config_applied_ = false;
  QueuedPickCanceller* pick_canceller_ = nullptr;
  LbCallState lb_call_state_;
  const LoadBalancingPolicy::BackendMetricData* backend_metric_data_ = nullptr;
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
  std::function<void(grpc_error*, LoadBalancingPolicy::MetadataInterface*,
                     LoadBalancingPolicy::CallState*)>
      lb_recv_trailing_metadata_ready_;
  grpc_closure pick_closure_;

  // For intercepting recv_trailing_metadata_ready for the LB policy.
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;

  grpc_polling_entity* pollent_ = nullptr;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the subchannel call and are not
  // intercepting any of its callbacks).
  PendingBatch pending_batches_[MAX_PENDING_BATCHES] = {};
  bool pending_send_initial_metadata_ : 1;
  bool pending_send_message_ : 1;
  bool pending_send_trailing_metadata_ : 1;

  // Retry state.
  bool enable_retries_ : 1;
  bool retry_committed_ : 1;
  bool last_attempt_got_server_pushback_ : 1;
  int num_attempts_completed_ = 0;
  size_t bytes_buffered_for_retry_ = 0;
  // TODO(roth): Restructure this to eliminate use of ManualConstructor.
  ManualConstructor<BackOff> retry_backoff_;
  grpc_timer retry_timer_;

  // The number of pending retriable subchannel batches containing send ops.
  // We hold a ref to the call stack while this is non-zero, since replay
  // batches may not complete until after all callbacks have been returned
  // to the surface, and we need to make sure that the call is not destroyed
  // until all of these batches have completed.
  // Note that we actually only need to track replay batches, but it's
  // easier to track all batches with send ops.
  int num_pending_retriable_subchannel_send_batches_ = 0;

  // Cached data for retrying send ops.
  // send_initial_metadata
  bool seen_send_initial_metadata_ = false;
  grpc_linked_mdelem* send_initial_metadata_storage_ = nullptr;
  grpc_metadata_batch send_initial_metadata_;
  uint32_t send_initial_metadata_flags_;
  gpr_atm* peer_string_;
  // send_message
  // When we get a send_message op, we replace the original byte stream
  // with a CachingByteStream that caches the slices to a local buffer for
  // use in retries.
  // Note: We inline the cache for the first 3 send_message ops and use
  // dynamic allocation after that.  This number was essentially picked
  // at random; it could be changed in the future to tune performance.
  absl::InlinedVector<ByteStreamCache*, 3> send_messages_;
  // send_trailing_metadata
  bool seen_send_trailing_metadata_ = false;
  grpc_linked_mdelem* send_trailing_metadata_storage_ = nullptr;
  grpc_metadata_batch send_trailing_metadata_;
};

//
// ChannelData::SubchannelWrapper
//

// This class is a wrapper for Subchannel that hides details of the
// channel's implementation (such as the health check service name and
// connected subchannel) from the LB policy API.
//
// Note that no synchronization is needed here, because even if the
// underlying subchannel is shared between channels, this wrapper will only
// be used within one channel, so it will always be synchronized by the
// control plane work_serializer.
class ChannelData::SubchannelWrapper : public SubchannelInterface {
 public:
  SubchannelWrapper(ChannelData* chand, Subchannel* subchannel,
                    grpc_core::UniquePtr<char> health_check_service_name)
      : SubchannelInterface(&grpc_client_channel_routing_trace),
        chand_(chand),
        subchannel_(subchannel),
        health_check_service_name_(std::move(health_check_service_name)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: creating subchannel wrapper %p for subchannel %p",
              chand, this, subchannel_);
    }
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "SubchannelWrapper");
    auto* subchannel_node = subchannel_->channelz_node();
    if (subchannel_node != nullptr) {
      auto it = chand_->subchannel_refcount_map_.find(subchannel_);
      if (it == chand_->subchannel_refcount_map_.end()) {
        chand_->channelz_node_->AddChildSubchannel(subchannel_node->uuid());
        it = chand_->subchannel_refcount_map_.emplace(subchannel_, 0).first;
      }
      ++it->second;
    }
    chand_->subchannel_wrappers_.insert(this);
  }

  ~SubchannelWrapper() {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: destroying subchannel wrapper %p for subchannel %p",
              chand_, this, subchannel_);
    }
    chand_->subchannel_wrappers_.erase(this);
    auto* subchannel_node = subchannel_->channelz_node();
    if (subchannel_node != nullptr) {
      auto it = chand_->subchannel_refcount_map_.find(subchannel_);
      GPR_ASSERT(it != chand_->subchannel_refcount_map_.end());
      --it->second;
      if (it->second == 0) {
        chand_->channelz_node_->RemoveChildSubchannel(subchannel_node->uuid());
        chand_->subchannel_refcount_map_.erase(it);
      }
    }
    GRPC_SUBCHANNEL_UNREF(subchannel_, "unref from LB");
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_, "SubchannelWrapper");
  }

  grpc_connectivity_state CheckConnectivityState() override {
    RefCountedPtr<ConnectedSubchannel> connected_subchannel;
    grpc_connectivity_state connectivity_state =
        subchannel_->CheckConnectivityState(health_check_service_name_.get(),
                                            &connected_subchannel);
    MaybeUpdateConnectedSubchannel(std::move(connected_subchannel));
    return connectivity_state;
  }

  void WatchConnectivityState(
      grpc_connectivity_state initial_state,
      std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override {
    auto& watcher_wrapper = watcher_map_[watcher.get()];
    GPR_ASSERT(watcher_wrapper == nullptr);
    watcher_wrapper = new WatcherWrapper(std::move(watcher),
                                         Ref(DEBUG_LOCATION, "WatcherWrapper"),
                                         initial_state);
    subchannel_->WatchConnectivityState(
        initial_state,
        grpc_core::UniquePtr<char>(
            gpr_strdup(health_check_service_name_.get())),
        RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface>(
            watcher_wrapper));
  }

  void CancelConnectivityStateWatch(
      ConnectivityStateWatcherInterface* watcher) override {
    auto it = watcher_map_.find(watcher);
    GPR_ASSERT(it != watcher_map_.end());
    subchannel_->CancelConnectivityStateWatch(health_check_service_name_.get(),
                                              it->second);
    watcher_map_.erase(it);
  }

  void AttemptToConnect() override { subchannel_->AttemptToConnect(); }

  void ResetBackoff() override { subchannel_->ResetBackoff(); }

  const grpc_channel_args* channel_args() override {
    return subchannel_->channel_args();
  }

  void UpdateHealthCheckServiceName(
      grpc_core::UniquePtr<char> health_check_service_name) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: subchannel wrapper %p: updating health check service "
              "name from \"%s\" to \"%s\"",
              chand_, this, health_check_service_name_.get(),
              health_check_service_name.get());
    }
    for (auto& p : watcher_map_) {
      WatcherWrapper*& watcher_wrapper = p.second;
      // Cancel the current watcher and create a new one using the new
      // health check service name.
      // TODO(roth): If there is not already an existing health watch
      // call for the new name, then the watcher will initially report
      // state CONNECTING.  If the LB policy is currently reporting
      // state READY, this may cause it to switch to CONNECTING before
      // switching back to READY.  This could cause a small delay for
      // RPCs being started on the channel.  If/when this becomes a
      // problem, we may be able to handle it by waiting for the new
      // watcher to report READY before we use it to replace the old one.
      WatcherWrapper* replacement = watcher_wrapper->MakeReplacement();
      subchannel_->CancelConnectivityStateWatch(
          health_check_service_name_.get(), watcher_wrapper);
      watcher_wrapper = replacement;
      subchannel_->WatchConnectivityState(
          replacement->last_seen_state(),
          grpc_core::UniquePtr<char>(
              gpr_strdup(health_check_service_name.get())),
          RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface>(
              replacement));
    }
    // Save the new health check service name.
    health_check_service_name_ = std::move(health_check_service_name);
  }

  // Caller must be holding the control-plane work_serializer.
  ConnectedSubchannel* connected_subchannel() const {
    return connected_subchannel_.get();
  }

  // Caller must be holding the data-plane mutex.
  ConnectedSubchannel* connected_subchannel_in_data_plane() const {
    return connected_subchannel_in_data_plane_.get();
  }
  void set_connected_subchannel_in_data_plane(
      RefCountedPtr<ConnectedSubchannel> connected_subchannel) {
    connected_subchannel_in_data_plane_ = std::move(connected_subchannel);
  }

 private:
  // Subchannel and SubchannelInterface have different interfaces for
  // their respective ConnectivityStateWatcherInterface classes.
  // The one in Subchannel updates the ConnectedSubchannel along with
  // the state, whereas the one in SubchannelInterface does not expose
  // the ConnectedSubchannel.
  //
  // This wrapper provides a bridge between the two.  It implements
  // Subchannel::ConnectivityStateWatcherInterface and wraps
  // the instance of SubchannelInterface::ConnectivityStateWatcherInterface
  // that was passed in by the LB policy.  We pass an instance of this
  // class to the underlying Subchannel, and when we get updates from
  // the subchannel, we pass those on to the wrapped watcher to return
  // the update to the LB policy.  This allows us to set the connected
  // subchannel before passing the result back to the LB policy.
  class WatcherWrapper : public Subchannel::ConnectivityStateWatcherInterface {
   public:
    WatcherWrapper(
        std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
            watcher,
        RefCountedPtr<SubchannelWrapper> parent,
        grpc_connectivity_state initial_state)
        : watcher_(std::move(watcher)),
          parent_(std::move(parent)),
          last_seen_state_(initial_state) {}

    ~WatcherWrapper() {
      auto* parent = parent_.release();  // ref owned by lambda
      parent->chand_->work_serializer_->Run(
          [parent]() { parent->Unref(DEBUG_LOCATION, "WatcherWrapper"); },
          DEBUG_LOCATION);
    }

    void OnConnectivityStateChange() override {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: connectivity change for subchannel wrapper %p "
                "subchannel %p; hopping into work_serializer",
                parent_->chand_, parent_.get(), parent_->subchannel_);
      }
      Ref().release();  // ref owned by lambda
      parent_->chand_->work_serializer_->Run(
          [this]() {
            ApplyUpdateInControlPlaneWorkSerializer();
            Unref();
          },
          DEBUG_LOCATION);
    }

    grpc_pollset_set* interested_parties() override {
      SubchannelInterface::ConnectivityStateWatcherInterface* watcher =
          watcher_.get();
      if (watcher_ == nullptr) watcher = replacement_->watcher_.get();
      return watcher->interested_parties();
    }

    WatcherWrapper* MakeReplacement() {
      auto* replacement =
          new WatcherWrapper(std::move(watcher_), parent_, last_seen_state_);
      replacement_ = replacement;
      return replacement;
    }

    grpc_connectivity_state last_seen_state() const { return last_seen_state_; }

   private:
    void ApplyUpdateInControlPlaneWorkSerializer() {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: processing connectivity change in work serializer "
                "for subchannel wrapper %p subchannel %p "
                "watcher=%p",
                parent_->chand_, parent_.get(), parent_->subchannel_,
                watcher_.get());
      }
      ConnectivityStateChange state_change = PopConnectivityStateChange();
      // Ignore update if the parent WatcherWrapper has been replaced
      // since this callback was scheduled.
      if (watcher_ != nullptr) {
        last_seen_state_ = state_change.state;
        parent_->MaybeUpdateConnectedSubchannel(
            std::move(state_change.connected_subchannel));
        watcher_->OnConnectivityStateChange(state_change.state);
      }
    }

    std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
        watcher_;
    RefCountedPtr<SubchannelWrapper> parent_;
    grpc_connectivity_state last_seen_state_;
    WatcherWrapper* replacement_ = nullptr;
  };

  void MaybeUpdateConnectedSubchannel(
      RefCountedPtr<ConnectedSubchannel> connected_subchannel) {
    // Update the connected subchannel only if the channel is not shutting
    // down.  This is because once the channel is shutting down, we
    // ignore picker updates from the LB policy, which means that
    // UpdateStateAndPickerLocked() will never process the entries
    // in chand_->pending_subchannel_updates_.  So we don't want to add
    // entries there that will never be processed, since that would
    // leave dangling refs to the channel and prevent its destruction.
    grpc_error* disconnect_error = chand_->disconnect_error();
    if (disconnect_error != GRPC_ERROR_NONE) return;
    // Not shutting down, so do the update.
    if (connected_subchannel_ != connected_subchannel) {
      connected_subchannel_ = std::move(connected_subchannel);
      // Record the new connected subchannel so that it can be updated
      // in the data plane mutex the next time the picker is updated.
      chand_->pending_subchannel_updates_[Ref(
          DEBUG_LOCATION, "ConnectedSubchannelUpdate")] = connected_subchannel_;
    }
  }

  ChannelData* chand_;
  Subchannel* subchannel_;
  grpc_core::UniquePtr<char> health_check_service_name_;
  // Maps from the address of the watcher passed to us by the LB policy
  // to the address of the WrapperWatcher that we passed to the underlying
  // subchannel.  This is needed so that when the LB policy calls
  // CancelConnectivityStateWatch() with its watcher, we know the
  // corresponding WrapperWatcher to cancel on the underlying subchannel.
  std::map<ConnectivityStateWatcherInterface*, WatcherWrapper*> watcher_map_;
  // To be accessed only in the control plane work_serializer.
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
  // To be accessed only in the data plane mutex.
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_in_data_plane_;
};

//
// ChannelData::ExternalConnectivityWatcher
//

ChannelData::ExternalConnectivityWatcher::ExternalConnectivityWatcher(
    ChannelData* chand, grpc_polling_entity pollent,
    grpc_connectivity_state* state, grpc_closure* on_complete,
    grpc_closure* watcher_timer_init)
    : chand_(chand),
      pollent_(pollent),
      initial_state_(*state),
      state_(state),
      on_complete_(on_complete),
      watcher_timer_init_(watcher_timer_init) {
  grpc_polling_entity_add_to_pollset_set(&pollent_,
                                         chand_->interested_parties_);
  GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ExternalConnectivityWatcher");
}

ChannelData::ExternalConnectivityWatcher::~ExternalConnectivityWatcher() {
  grpc_polling_entity_del_from_pollset_set(&pollent_,
                                           chand_->interested_parties_);
  GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_,
                           "ExternalConnectivityWatcher");
}

void ChannelData::ExternalConnectivityWatcher::Start() {
  chand_->work_serializer_->Run([this]() { AddWatcherLocked(); },
                                DEBUG_LOCATION);
}

void ChannelData::ExternalConnectivityWatcher::Notify(
    grpc_connectivity_state state) {
  bool done = false;
  if (!done_.CompareExchangeStrong(&done, true, MemoryOrder::RELAXED,
                                   MemoryOrder::RELAXED)) {
    return;  // Already done.
  }
  // Remove external watcher.
  chand_->RemoveExternalConnectivityWatcher(on_complete_, /*cancel=*/false);
  // Report new state to the user.
  *state_ = state;
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, GRPC_ERROR_NONE);
  // Hop back into the work_serializer to clean up.
  // Not needed in state SHUTDOWN, because the tracker will
  // automatically remove all watchers in that case.
  if (state != GRPC_CHANNEL_SHUTDOWN) {
    chand_->work_serializer_->Run([this]() { RemoveWatcherLocked(); },
                                  DEBUG_LOCATION);
  }
}

void ChannelData::ExternalConnectivityWatcher::Cancel() {
  bool done = false;
  if (!done_.CompareExchangeStrong(&done, true, MemoryOrder::RELAXED,
                                   MemoryOrder::RELAXED)) {
    return;  // Already done.
  }
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, GRPC_ERROR_CANCELLED);
  // Hop back into the work_serializer to clean up.
  chand_->work_serializer_->Run([this]() { RemoveWatcherLocked(); },
                                DEBUG_LOCATION);
}

void ChannelData::ExternalConnectivityWatcher::AddWatcherLocked() {
  Closure::Run(DEBUG_LOCATION, watcher_timer_init_, GRPC_ERROR_NONE);
  // Add new watcher.
  chand_->state_tracker_.AddWatcher(
      initial_state_, OrphanablePtr<ConnectivityStateWatcherInterface>(this));
}

void ChannelData::ExternalConnectivityWatcher::RemoveWatcherLocked() {
  chand_->state_tracker_.RemoveWatcher(this);
}

//
// ChannelData::ConnectivityWatcherAdder
//

class ChannelData::ConnectivityWatcherAdder {
 public:
  ConnectivityWatcherAdder(
      ChannelData* chand, grpc_connectivity_state initial_state,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher)
      : chand_(chand),
        initial_state_(initial_state),
        watcher_(std::move(watcher)) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ConnectivityWatcherAdder");
    chand_->work_serializer_->Run([this]() { AddWatcherLocked(); },
                                  DEBUG_LOCATION);
  }

 private:
  void AddWatcherLocked() {
    chand_->state_tracker_.AddWatcher(initial_state_, std::move(watcher_));
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_, "ConnectivityWatcherAdder");
    delete this;
  }

  ChannelData* chand_;
  grpc_connectivity_state initial_state_;
  OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher_;
};

//
// ChannelData::ConnectivityWatcherRemover
//

class ChannelData::ConnectivityWatcherRemover {
 public:
  ConnectivityWatcherRemover(ChannelData* chand,
                             AsyncConnectivityStateWatcherInterface* watcher)
      : chand_(chand), watcher_(watcher) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ConnectivityWatcherRemover");
    chand_->work_serializer_->Run([this]() { RemoveWatcherLocked(); },
                                  DEBUG_LOCATION);
  }

 private:
  void RemoveWatcherLocked() {
    chand_->state_tracker_.RemoveWatcher(watcher_);
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_,
                             "ConnectivityWatcherRemover");
    delete this;
  }

  ChannelData* chand_;
  AsyncConnectivityStateWatcherInterface* watcher_;
};

//
// ChannelData::ClientChannelControlHelper
//

class ChannelData::ClientChannelControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ClientChannelControlHelper(ChannelData* chand) : chand_(chand) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ClientChannelControlHelper");
  }

  ~ClientChannelControlHelper() override {
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_,
                             "ClientChannelControlHelper");
  }

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      const grpc_channel_args& args) override {
    bool inhibit_health_checking = grpc_channel_arg_get_bool(
        grpc_channel_args_find(&args, GRPC_ARG_INHIBIT_HEALTH_CHECKING), false);
    grpc_core::UniquePtr<char> health_check_service_name;
    if (!inhibit_health_checking) {
      health_check_service_name.reset(
          gpr_strdup(chand_->health_check_service_name_.get()));
    }
    static const char* args_to_remove[] = {
        GRPC_ARG_INHIBIT_HEALTH_CHECKING,
        GRPC_ARG_CHANNELZ_CHANNEL_NODE,
    };
    grpc_arg arg = SubchannelPoolInterface::CreateChannelArg(
        chand_->subchannel_pool_.get());
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        &args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &arg, 1);
    Subchannel* subchannel =
        chand_->client_channel_factory_->CreateSubchannel(new_args);
    grpc_channel_args_destroy(new_args);
    if (subchannel == nullptr) return nullptr;
    return MakeRefCounted<SubchannelWrapper>(
        chand_, subchannel, std::move(health_check_service_name));
  }

  void UpdateState(
      grpc_connectivity_state state,
      std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker) override {
    grpc_error* disconnect_error = chand_->disconnect_error();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      const char* extra = disconnect_error == GRPC_ERROR_NONE
                              ? ""
                              : " (ignoring -- channel shutting down)";
      gpr_log(GPR_INFO, "chand=%p: update: state=%s picker=%p%s", chand_,
              ConnectivityStateName(state), picker.get(), extra);
    }
    // Do update only if not shutting down.
    if (disconnect_error == GRPC_ERROR_NONE) {
      chand_->UpdateStateAndPickerLocked(state, "helper", std::move(picker));
    }
  }

  // No-op -- we should never get this from ResolvingLoadBalancingPolicy.
  void RequestReresolution() override {}

  void AddTraceEvent(TraceSeverity severity,
                     absl::string_view message) override {
    if (chand_->channelz_node_ != nullptr) {
      chand_->channelz_node_->AddTraceEvent(
          ConvertSeverityEnum(severity),
          grpc_slice_from_copied_buffer(message.data(), message.size()));
    }
  }

 private:
  static channelz::ChannelTrace::Severity ConvertSeverityEnum(
      TraceSeverity severity) {
    if (severity == TRACE_INFO) return channelz::ChannelTrace::Info;
    if (severity == TRACE_WARNING) return channelz::ChannelTrace::Warning;
    return channelz::ChannelTrace::Error;
  }

  ChannelData* chand_;
};

//
// ChannelData implementation
//

grpc_error* ChannelData::Init(grpc_channel_element* elem,
                              grpc_channel_element_args* args) {
  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  grpc_error* error = GRPC_ERROR_NONE;
  new (elem->channel_data) ChannelData(args, &error);
  return error;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

bool GetEnableRetries(const grpc_channel_args* args) {
  return grpc_channel_arg_get_bool(
      grpc_channel_args_find(args, GRPC_ARG_ENABLE_RETRIES), true);
}

size_t GetMaxPerRpcRetryBufferSize(const grpc_channel_args* args) {
  return static_cast<size_t>(grpc_channel_arg_get_integer(
      grpc_channel_args_find(args, GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE),
      {DEFAULT_PER_RPC_RETRY_BUFFER_SIZE, 0, INT_MAX}));
}

RefCountedPtr<SubchannelPoolInterface> GetSubchannelPool(
    const grpc_channel_args* args) {
  const bool use_local_subchannel_pool = grpc_channel_arg_get_bool(
      grpc_channel_args_find(args, GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL), false);
  if (use_local_subchannel_pool) {
    return MakeRefCounted<LocalSubchannelPool>();
  }
  return GlobalSubchannelPool::instance();
}

channelz::ChannelNode* GetChannelzNode(const grpc_channel_args* args) {
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
  if (arg != nullptr && arg->type == GRPC_ARG_POINTER) {
    return static_cast<channelz::ChannelNode*>(arg->value.pointer.p);
  }
  return nullptr;
}

ChannelData::ChannelData(grpc_channel_element_args* args, grpc_error** error)
    : deadline_checking_enabled_(
          grpc_deadline_checking_enabled(args->channel_args)),
      enable_retries_(GetEnableRetries(args->channel_args)),
      per_rpc_retry_buffer_size_(
          GetMaxPerRpcRetryBufferSize(args->channel_args)),
      owning_stack_(args->channel_stack),
      client_channel_factory_(
          ClientChannelFactory::GetFromChannelArgs(args->channel_args)),
      channelz_node_(GetChannelzNode(args->channel_args)),
      work_serializer_(std::make_shared<WorkSerializer>()),
      interested_parties_(grpc_pollset_set_create()),
      subchannel_pool_(GetSubchannelPool(args->channel_args)),
      state_tracker_("client_channel", GRPC_CHANNEL_IDLE),
      disconnect_error_(GRPC_ERROR_NONE) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: creating client_channel for channel stack %p",
            this, owning_stack_);
  }
  // Initialize data members.
  gpr_mu_init(&info_mu_);
  // Start backup polling.
  grpc_client_channel_start_backup_polling(interested_parties_);
  // Check client channel factory.
  if (client_channel_factory_ == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Missing client channel factory in args for client channel filter");
    return;
  }
  // Get server name to resolve, using proxy mapper if needed.
  const char* server_uri = grpc_channel_arg_get_string(
      grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVER_URI));
  if (server_uri == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "server URI channel arg missing or wrong type in client channel "
        "filter");
    return;
  }
  // Get default service config
  const char* service_config_json = grpc_channel_arg_get_string(
      grpc_channel_args_find(args->channel_args, GRPC_ARG_SERVICE_CONFIG));
  if (service_config_json != nullptr) {
    *error = GRPC_ERROR_NONE;
    default_service_config_ = ServiceConfig::Create(service_config_json, error);
    if (*error != GRPC_ERROR_NONE) {
      default_service_config_.reset();
      return;
    }
  }
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  if (uri != nullptr && uri->path[0] != '\0') {
    server_name_.reset(
        gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path));
  }
  grpc_uri_destroy(uri);
  char* proxy_name = nullptr;
  grpc_channel_args* new_args = nullptr;
  ProxyMapperRegistry::MapName(server_uri, args->channel_args, &proxy_name,
                               &new_args);
  target_uri_.reset(proxy_name != nullptr ? proxy_name
                                          : gpr_strdup(server_uri));
  channel_args_ = new_args != nullptr
                      ? new_args
                      : grpc_channel_args_copy(args->channel_args);
  if (!ResolverRegistry::IsValidTarget(target_uri_.get())) {
    *error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("the target uri is not valid.");
    return;
  }
  *error = GRPC_ERROR_NONE;
}

ChannelData::~ChannelData() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: destroying channel", this);
  }
  DestroyResolvingLoadBalancingPolicyLocked();
  grpc_channel_args_destroy(channel_args_);
  // Stop backup polling.
  grpc_client_channel_stop_backup_polling(interested_parties_);
  grpc_pollset_set_destroy(interested_parties_);
  GRPC_ERROR_UNREF(disconnect_error_.Load(MemoryOrder::RELAXED));
  gpr_mu_destroy(&info_mu_);
}

void ChannelData::UpdateStateAndPickerLocked(
    grpc_connectivity_state state, const char* reason,
    std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker) {
  // Clean the control plane when entering IDLE.
  if (picker_ == nullptr) {
    health_check_service_name_.reset();
    saved_service_config_.reset();
    received_first_resolver_result_ = false;
  }
  // Update connectivity state.
  state_tracker_.SetState(state, reason);
  if (channelz_node_ != nullptr) {
    channelz_node_->SetConnectivityState(state);
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            channelz::ChannelNode::GetChannelConnectivityStateChangeString(
                state)));
  }
  // Grab data plane lock to do subchannel updates and update the picker.
  //
  // Note that we want to minimize the work done while holding the data
  // plane lock, to keep the critical section small.  So, for all of the
  // objects that we might wind up unreffing here, we actually hold onto
  // the refs until after we release the lock, and then unref them at
  // that point.  This includes the following:
  // - refs to subchannel wrappers in the keys of pending_subchannel_updates_
  // - ref stored in retry_throttle_data_
  // - ref stored in service_config_
  // - ownership of the existing picker in picker_
  RefCountedPtr<ServerRetryThrottleData> retry_throttle_data_to_unref;
  RefCountedPtr<ServiceConfig> service_config_to_unref;
  {
    MutexLock lock(&data_plane_mu_);
    // Handle subchannel updates.
    for (auto& p : pending_subchannel_updates_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: updating subchannel wrapper %p data plane "
                "connected_subchannel to %p",
                this, p.first.get(), p.second.get());
      }
      // Note: We do not remove the entry from pending_subchannel_updates_
      // here, since this would unref the subchannel wrapper; instead,
      // we wait until we've released the lock to clear the map.
      p.first->set_connected_subchannel_in_data_plane(std::move(p.second));
    }
    // Swap out the picker.
    // Note: Original value will be destroyed after the lock is released.
    picker_.swap(picker);
    // Clean the data plane if the updated picker is nullptr.
    if (picker_ == nullptr) {
      received_service_config_data_ = false;
      // Note: We save the objects to unref until after the lock is released.
      retry_throttle_data_to_unref = std::move(retry_throttle_data_);
      service_config_to_unref = std::move(service_config_);
    }
    // Re-process queued picks.
    for (QueuedPick* pick = queued_picks_; pick != nullptr; pick = pick->next) {
      grpc_call_element* elem = pick->elem;
      CallData* calld = static_cast<CallData*>(elem->call_data);
      grpc_error* error = GRPC_ERROR_NONE;
      if (calld->PickSubchannelLocked(elem, &error)) {
        calld->AsyncPickDone(elem, error);
      }
    }
  }
  // Clear the pending update map after releasing the lock, to keep the
  // critical section small.
  pending_subchannel_updates_.clear();
}

void ChannelData::UpdateServiceConfigLocked(
    RefCountedPtr<ServerRetryThrottleData> retry_throttle_data,
    RefCountedPtr<ServiceConfig> service_config) {
  // Grab data plane lock to update service config.
  //
  // We defer unreffing the old values (and deallocating memory) until
  // after releasing the lock to keep the critical section small.
  {
    MutexLock lock(&data_plane_mu_);
    // Update service config.
    received_service_config_data_ = true;
    // Old values will be unreffed after lock is released.
    retry_throttle_data_.swap(retry_throttle_data);
    service_config_.swap(service_config);
    // Apply service config to queued picks.
    for (QueuedPick* pick = queued_picks_; pick != nullptr; pick = pick->next) {
      CallData* calld = static_cast<CallData*>(pick->elem->call_data);
      calld->MaybeApplyServiceConfigToCallLocked(pick->elem);
    }
  }
  // Old values will be unreffed after lock is released when they go out
  // of scope.
}

void ChannelData::CreateResolvingLoadBalancingPolicyLocked() {
  // Instantiate resolving LB policy.
  LoadBalancingPolicy::Args lb_args;
  lb_args.work_serializer = work_serializer_;
  lb_args.channel_control_helper =
      absl::make_unique<ClientChannelControlHelper>(this);
  lb_args.args = channel_args_;
  grpc_core::UniquePtr<char> target_uri(gpr_strdup(target_uri_.get()));
  resolving_lb_policy_.reset(new ResolvingLoadBalancingPolicy(
      std::move(lb_args), &grpc_client_channel_routing_trace,
      std::move(target_uri), ProcessResolverResultLocked, this));
  grpc_pollset_set_add_pollset_set(resolving_lb_policy_->interested_parties(),
                                   interested_parties_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: created resolving_lb_policy=%p", this,
            resolving_lb_policy_.get());
  }
}

void ChannelData::DestroyResolvingLoadBalancingPolicyLocked() {
  if (resolving_lb_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(resolving_lb_policy_->interested_parties(),
                                     interested_parties_);
    resolving_lb_policy_.reset();
  }
}

void ChannelData::ProcessLbPolicy(
    const Resolver::Result& resolver_result,
    const internal::ClientChannelGlobalParsedConfig* parsed_service_config,
    RefCountedPtr<LoadBalancingPolicy::Config>* lb_policy_config) {
  // Prefer the LB policy config found in the service config.
  if (parsed_service_config != nullptr &&
      parsed_service_config->parsed_lb_config() != nullptr) {
    *lb_policy_config = parsed_service_config->parsed_lb_config();
    return;
  }
  // Try the deprecated LB policy name from the service config.
  // If not, try the setting from channel args.
  const char* policy_name = nullptr;
  if (parsed_service_config != nullptr &&
      !parsed_service_config->parsed_deprecated_lb_policy().empty()) {
    policy_name = parsed_service_config->parsed_deprecated_lb_policy().c_str();
  } else {
    const grpc_arg* channel_arg =
        grpc_channel_args_find(resolver_result.args, GRPC_ARG_LB_POLICY_NAME);
    policy_name = grpc_channel_arg_get_string(channel_arg);
  }
  // Use pick_first if nothing was specified and we didn't select grpclb
  // above.
  if (policy_name == nullptr) policy_name = "pick_first";
  // Now that we have the policy name, construct an empty config for it.
  Json config_json = Json::Array{Json::Object{
      {policy_name, Json::Object{}},
  }};
  grpc_error* parse_error = GRPC_ERROR_NONE;
  *lb_policy_config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
      config_json, &parse_error);
  // The policy name came from one of three places:
  // - The deprecated loadBalancingPolicy field in the service config,
  //   in which case the code in ClientChannelServiceConfigParser
  //   already verified that the policy does not require a config.
  // - One of the hard-coded values here, all of which are known to not
  //   require a config.
  // - A channel arg, in which case the application did something that
  //   is a misuse of our API.
  // In the first two cases, these assertions will always be true.  In
  // the last case, this is probably fine for now.
  // TODO(roth): If the last case becomes a problem, add better error
  // handling here.
  GPR_ASSERT(*lb_policy_config != nullptr);
  GPR_ASSERT(parse_error == GRPC_ERROR_NONE);
}

// Synchronous callback from ResolvingLoadBalancingPolicy to process a
// resolver result update.
bool ChannelData::ProcessResolverResultLocked(
    void* arg, const Resolver::Result& result,
    RefCountedPtr<LoadBalancingPolicy::Config>* lb_policy_config,
    grpc_error** service_config_error, bool* no_valid_service_config) {
  ChannelData* chand = static_cast<ChannelData*>(arg);
  RefCountedPtr<ServiceConfig> service_config;
  // If resolver did not return a service config or returned an invalid service
  // config, we need a fallback service config.
  if (result.service_config_error != GRPC_ERROR_NONE) {
    // If the service config was invalid, then fallback to the saved service
    // config. If there is no saved config either, use the default service
    // config.
    if (chand->saved_service_config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: resolver returned invalid service config. "
                "Continuing to use previous service config.",
                chand);
      }
      service_config = chand->saved_service_config_;
    } else if (chand->default_service_config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: resolver returned invalid service config. Using "
                "default service config provided by client API.",
                chand);
      }
      service_config = chand->default_service_config_;
    }
  } else if (result.service_config == nullptr) {
    if (chand->default_service_config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: resolver returned no service config. Using default "
                "service config provided by client API.",
                chand);
      }
      service_config = chand->default_service_config_;
    }
  } else {
    service_config = result.service_config;
  }
  *service_config_error = GRPC_ERROR_REF(result.service_config_error);
  if (service_config == nullptr &&
      result.service_config_error != GRPC_ERROR_NONE) {
    *no_valid_service_config = true;
    return false;
  }
  // Process service config.
  grpc_core::UniquePtr<char> service_config_json;
  const internal::ClientChannelGlobalParsedConfig* parsed_service_config =
      nullptr;
  if (service_config != nullptr) {
    parsed_service_config =
        static_cast<const internal::ClientChannelGlobalParsedConfig*>(
            service_config->GetGlobalParsedConfig(
                internal::ClientChannelServiceConfigParser::ParserIndex()));
  }
  // Check if the config has changed.
  const bool service_config_changed =
      ((service_config == nullptr) !=
       (chand->saved_service_config_ == nullptr)) ||
      (service_config != nullptr &&
       service_config->json_string() !=
           chand->saved_service_config_->json_string());
  if (service_config_changed) {
    service_config_json.reset(gpr_strdup(
        service_config != nullptr ? service_config->json_string().c_str()
                                  : ""));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: resolver returned updated service config: \"%s\"",
              chand, service_config_json.get());
    }
    // Save health check service name.
    if (service_config != nullptr) {
      chand->health_check_service_name_.reset(
          gpr_strdup(parsed_service_config->health_check_service_name()));
    } else {
      chand->health_check_service_name_.reset();
    }
    // Update health check service name used by existing subchannel wrappers.
    for (auto* subchannel_wrapper : chand->subchannel_wrappers_) {
      subchannel_wrapper->UpdateHealthCheckServiceName(
          grpc_core::UniquePtr<char>(
              gpr_strdup(chand->health_check_service_name_.get())));
    }
    // Save service config.
    chand->saved_service_config_ = std::move(service_config);
  }
  // We want to set the service config at least once. This should not really be
  // needed, but we are doing it as a defensive approach. This can be removed,
  // if we feel it is unnecessary.
  if (service_config_changed || !chand->received_first_resolver_result_) {
    chand->received_first_resolver_result_ = true;
    RefCountedPtr<ServerRetryThrottleData> retry_throttle_data;
    if (parsed_service_config != nullptr) {
      absl::optional<internal::ClientChannelGlobalParsedConfig::RetryThrottling>
          retry_throttle_config = parsed_service_config->retry_throttling();
      if (retry_throttle_config.has_value()) {
        retry_throttle_data =
            internal::ServerRetryThrottleMap::GetDataForServer(
                chand->server_name_.get(),
                retry_throttle_config.value().max_milli_tokens,
                retry_throttle_config.value().milli_token_ratio);
      }
    }
    chand->UpdateServiceConfigLocked(std::move(retry_throttle_data),
                                     chand->saved_service_config_);
  }
  chand->ProcessLbPolicy(result, parsed_service_config, lb_policy_config);
  grpc_core::UniquePtr<char> lb_policy_name(
      gpr_strdup((*lb_policy_config)->name()));
  // Swap out the data used by GetChannelInfo().
  {
    MutexLock lock(&chand->info_mu_);
    chand->info_lb_policy_name_ = std::move(lb_policy_name);
    if (service_config_json != nullptr) {
      chand->info_service_config_json_ = std::move(service_config_json);
    }
  }
  // Return results.
  return service_config_changed;
}

grpc_error* ChannelData::DoPingLocked(grpc_transport_op* op) {
  if (state_tracker_.state() != GRPC_CHANNEL_READY) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("channel not connected");
  }
  LoadBalancingPolicy::PickResult result =
      picker_->Pick(LoadBalancingPolicy::PickArgs());
  ConnectedSubchannel* connected_subchannel = nullptr;
  if (result.subchannel != nullptr) {
    SubchannelWrapper* subchannel =
        static_cast<SubchannelWrapper*>(result.subchannel.get());
    connected_subchannel = subchannel->connected_subchannel();
  }
  if (connected_subchannel != nullptr) {
    connected_subchannel->Ping(op->send_ping.on_initiate, op->send_ping.on_ack);
  } else {
    if (result.error == GRPC_ERROR_NONE) {
      result.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "LB policy dropped call on ping");
    }
  }
  return result.error;
}

void ChannelData::StartTransportOpLocked(grpc_transport_op* op) {
  // Connectivity watch.
  if (op->start_connectivity_watch != nullptr) {
    state_tracker_.AddWatcher(op->start_connectivity_watch_state,
                              std::move(op->start_connectivity_watch));
  }
  if (op->stop_connectivity_watch != nullptr) {
    state_tracker_.RemoveWatcher(op->stop_connectivity_watch);
  }
  // Ping.
  if (op->send_ping.on_initiate != nullptr || op->send_ping.on_ack != nullptr) {
    grpc_error* error = DoPingLocked(op);
    if (error != GRPC_ERROR_NONE) {
      ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_initiate,
                   GRPC_ERROR_REF(error));
      ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_ack, error);
    }
    op->bind_pollset = nullptr;
    op->send_ping.on_initiate = nullptr;
    op->send_ping.on_ack = nullptr;
  }
  // Reset backoff.
  if (op->reset_connect_backoff) {
    if (resolving_lb_policy_ != nullptr) {
      resolving_lb_policy_->ResetBackoffLocked();
    }
  }
  // Disconnect or enter IDLE.
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p: disconnect_with_error: %s", this,
              grpc_error_string(op->disconnect_with_error));
    }
    DestroyResolvingLoadBalancingPolicyLocked();
    intptr_t value;
    if (grpc_error_get_int(op->disconnect_with_error,
                           GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE, &value) &&
        static_cast<grpc_connectivity_state>(value) == GRPC_CHANNEL_IDLE) {
      if (disconnect_error() == GRPC_ERROR_NONE) {
        // Enter IDLE state.
        UpdateStateAndPickerLocked(GRPC_CHANNEL_IDLE, "channel entering IDLE",
                                   nullptr);
      }
      GRPC_ERROR_UNREF(op->disconnect_with_error);
    } else {
      // Disconnect.
      GPR_ASSERT(disconnect_error_.Load(MemoryOrder::RELAXED) ==
                 GRPC_ERROR_NONE);
      disconnect_error_.Store(op->disconnect_with_error, MemoryOrder::RELEASE);
      UpdateStateAndPickerLocked(
          GRPC_CHANNEL_SHUTDOWN, "shutdown from API",
          absl::make_unique<LoadBalancingPolicy::TransientFailurePicker>(
              GRPC_ERROR_REF(op->disconnect_with_error)));
    }
  }
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "start_transport_op");
  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
}

void ChannelData::StartTransportOp(grpc_channel_element* elem,
                                   grpc_transport_op* op) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  GPR_ASSERT(op->set_accept_stream == false);
  // Handle bind_pollset.
  if (op->bind_pollset != nullptr) {
    grpc_pollset_set_add_pollset(chand->interested_parties_, op->bind_pollset);
  }
  // Pop into control plane work_serializer for remaining ops.
  GRPC_CHANNEL_STACK_REF(chand->owning_stack_, "start_transport_op");
  chand->work_serializer_->Run(
      [chand, op]() { chand->StartTransportOpLocked(op); }, DEBUG_LOCATION);
}

void ChannelData::GetChannelInfo(grpc_channel_element* elem,
                                 const grpc_channel_info* info) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  MutexLock lock(&chand->info_mu_);
  if (info->lb_policy_name != nullptr) {
    *info->lb_policy_name = gpr_strdup(chand->info_lb_policy_name_.get());
  }
  if (info->service_config_json != nullptr) {
    *info->service_config_json =
        gpr_strdup(chand->info_service_config_json_.get());
  }
}

void ChannelData::AddQueuedPick(QueuedPick* pick,
                                grpc_polling_entity* pollent) {
  // Add call to queued picks list.
  pick->next = queued_picks_;
  queued_picks_ = pick;
  // Add call's pollent to channel's interested_parties, so that I/O
  // can be done under the call's CQ.
  grpc_polling_entity_add_to_pollset_set(pollent, interested_parties_);
}

void ChannelData::RemoveQueuedPick(QueuedPick* to_remove,
                                   grpc_polling_entity* pollent) {
  // Remove call's pollent from channel's interested_parties.
  grpc_polling_entity_del_from_pollset_set(pollent, interested_parties_);
  // Remove from queued picks list.
  for (QueuedPick** pick = &queued_picks_; *pick != nullptr;
       pick = &(*pick)->next) {
    if (*pick == to_remove) {
      *pick = to_remove->next;
      return;
    }
  }
}

RefCountedPtr<ConnectedSubchannel>
ChannelData::GetConnectedSubchannelInDataPlane(
    SubchannelInterface* subchannel) const {
  SubchannelWrapper* subchannel_wrapper =
      static_cast<SubchannelWrapper*>(subchannel);
  ConnectedSubchannel* connected_subchannel =
      subchannel_wrapper->connected_subchannel_in_data_plane();
  if (connected_subchannel == nullptr) return nullptr;
  return connected_subchannel->Ref();
}

void ChannelData::TryToConnectLocked() {
  if (resolving_lb_policy_ != nullptr) {
    resolving_lb_policy_->ExitIdleLocked();
  } else {
    CreateResolvingLoadBalancingPolicyLocked();
  }
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "TryToConnect");
}

grpc_connectivity_state ChannelData::CheckConnectivityState(
    bool try_to_connect) {
  grpc_connectivity_state out = state_tracker_.state();
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    GRPC_CHANNEL_STACK_REF(owning_stack_, "TryToConnect");
    work_serializer_->Run([this]() { TryToConnectLocked(); }, DEBUG_LOCATION);
  }
  return out;
}

void ChannelData::AddConnectivityWatcher(
    grpc_connectivity_state initial_state,
    OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher) {
  new ConnectivityWatcherAdder(this, initial_state, std::move(watcher));
}

void ChannelData::RemoveConnectivityWatcher(
    AsyncConnectivityStateWatcherInterface* watcher) {
  new ConnectivityWatcherRemover(this, watcher);
}

//
// CallData implementation
//

// Retry support:
//
// In order to support retries, we act as a proxy for stream op batches.
// When we get a batch from the surface, we add it to our list of pending
// batches, and we then use those batches to construct separate "child"
// batches to be started on the subchannel call.  When the child batches
// return, we then decide which pending batches have been completed and
// schedule their callbacks accordingly.  If a subchannel call fails and
// we want to retry it, we do a new pick and start again, constructing
// new "child" batches for the new subchannel call.
//
// Note that retries are committed when receiving data from the server
// (except for Trailers-Only responses).  However, there may be many
// send ops started before receiving any data, so we may have already
// completed some number of send ops (and returned the completions up to
// the surface) by the time we realize that we need to retry.  To deal
// with this, we cache data for send ops, so that we can replay them on a
// different subchannel call even after we have completed the original
// batches.
//
// There are two sets of data to maintain:
// - In call_data (in the parent channel), we maintain a list of pending
//   ops and cached data for send ops.
// - In the subchannel call, we maintain state to indicate what ops have
//   already been sent down to that call.
//
// When constructing the "child" batches, we compare those two sets of
// data to see which batches need to be sent to the subchannel call.

// TODO(roth): In subsequent PRs:
// - add support for transparent retries (including initial metadata)
// - figure out how to record stats in census for retries
//   (census filter is on top of this one)
// - add census stats for retries

CallData::CallData(grpc_call_element* elem, const ChannelData& chand,
                   const grpc_call_element_args& args)
    : deadline_state_(elem, args.call_stack, args.call_combiner,
                      GPR_LIKELY(chand.deadline_checking_enabled())
                          ? args.deadline
                          : GRPC_MILLIS_INF_FUTURE),
      path_(grpc_slice_ref_internal(args.path)),
      call_start_time_(args.start_time),
      deadline_(args.deadline),
      arena_(args.arena),
      owning_call_(args.call_stack),
      call_combiner_(args.call_combiner),
      call_context_(args.context),
      lb_call_state_(this),
      pending_send_initial_metadata_(false),
      pending_send_message_(false),
      pending_send_trailing_metadata_(false),
      enable_retries_(chand.enable_retries()),
      retry_committed_(false),
      last_attempt_got_server_pushback_(false) {}

CallData::~CallData() {
  grpc_slice_unref_internal(path_);
  GRPC_ERROR_UNREF(cancel_error_);
  if (backend_metric_data_ != nullptr) {
    backend_metric_data_
        ->LoadBalancingPolicy::BackendMetricData::~BackendMetricData();
  }
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i].batch == nullptr);
  }
}

grpc_error* CallData::Init(grpc_call_element* elem,
                           const grpc_call_element_args* args) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  new (elem->call_data) CallData(elem, *chand, *args);
  return GRPC_ERROR_NONE;
}

void CallData::Destroy(grpc_call_element* elem,
                       const grpc_call_final_info* /*final_info*/,
                       grpc_closure* then_schedule_closure) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (GPR_LIKELY(calld->subchannel_call_ != nullptr)) {
    calld->subchannel_call_->SetAfterCallStackDestroy(then_schedule_closure);
    then_schedule_closure = nullptr;
  }
  calld->~CallData();
  // TODO(yashkt) : This can potentially be a Closure::Run
  ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, GRPC_ERROR_NONE);
}

void CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("cc_start_transport_stream_op_batch", 0);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (GPR_LIKELY(chand->deadline_checking_enabled())) {
    grpc_deadline_state_client_start_transport_stream_op_batch(elem, batch);
  }
  // If we've previously been cancelled, immediately fail any new batches.
  if (GPR_UNLIKELY(calld->cancel_error_ != GRPC_ERROR_NONE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: failing batch with error: %s",
              chand, calld, grpc_error_string(calld->cancel_error_));
    }
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(calld->cancel_error_), calld->call_combiner_);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    GRPC_ERROR_UNREF(calld->cancel_error_);
    calld->cancel_error_ =
        GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: recording cancel_error=%s", chand,
              calld, grpc_error_string(calld->cancel_error_));
    }
    // If we do not have a subchannel call (i.e., a pick has not yet
    // been started), fail all pending batches.  Otherwise, send the
    // cancellation down to the subchannel call.
    if (calld->subchannel_call_ == nullptr) {
      // TODO(roth): If there is a pending retry callback, do we need to
      // cancel it here?
      calld->PendingBatchesFail(elem, GRPC_ERROR_REF(calld->cancel_error_),
                                NoYieldCallCombiner);
      // Note: This will release the call combiner.
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(calld->cancel_error_), calld->call_combiner_);
    } else {
      // Note: This will release the call combiner.
      calld->subchannel_call_->StartTransportStreamOpBatch(batch);
    }
    return;
  }
  // Add the batch to the pending list.
  calld->PendingBatchesAdd(elem, batch);
  // Check if we've already gotten a subchannel call.
  // Note that once we have picked a subchannel, we do not need to acquire
  // the channel's data plane mutex, which is more efficient (especially for
  // streaming calls).
  if (calld->subchannel_call_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: starting batch on subchannel_call=%p", chand,
              calld, calld->subchannel_call_.get());
    }
    calld->PendingBatchesResume(elem);
    return;
  }
  // We do not yet have a subchannel call.
  // For batches containing a send_initial_metadata op, acquire the
  // channel's data plane mutex to pick a subchannel.
  if (GPR_LIKELY(batch->send_initial_metadata)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: grabbing data plane mutex to perform pick",
              chand, calld);
    }
    PickSubchannel(elem, GRPC_ERROR_NONE);
  } else {
    // For all other batches, release the call combiner.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: saved batch, yielding call combiner", chand,
              calld);
    }
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "batch does not include send_initial_metadata");
  }
}

void CallData::SetPollent(grpc_call_element* elem,
                          grpc_polling_entity* pollent) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->pollent_ = pollent;
}

//
// send op data caching
//

void CallData::MaybeCacheSendOpsForBatch(PendingBatch* pending) {
  if (pending->send_ops_cached) return;
  pending->send_ops_cached = true;
  grpc_transport_stream_op_batch* batch = pending->batch;
  // Save a copy of metadata for send_initial_metadata ops.
  if (batch->send_initial_metadata) {
    seen_send_initial_metadata_ = true;
    GPR_ASSERT(send_initial_metadata_storage_ == nullptr);
    grpc_metadata_batch* send_initial_metadata =
        batch->payload->send_initial_metadata.send_initial_metadata;
    send_initial_metadata_storage_ = (grpc_linked_mdelem*)arena_->Alloc(
        sizeof(grpc_linked_mdelem) * send_initial_metadata->list.count);
    grpc_metadata_batch_copy(send_initial_metadata, &send_initial_metadata_,
                             send_initial_metadata_storage_);
    send_initial_metadata_flags_ =
        batch->payload->send_initial_metadata.send_initial_metadata_flags;
    peer_string_ = batch->payload->send_initial_metadata.peer_string;
  }
  // Set up cache for send_message ops.
  if (batch->send_message) {
    ByteStreamCache* cache = arena_->New<ByteStreamCache>(
        std::move(batch->payload->send_message.send_message));
    send_messages_.push_back(cache);
  }
  // Save metadata batch for send_trailing_metadata ops.
  if (batch->send_trailing_metadata) {
    seen_send_trailing_metadata_ = true;
    GPR_ASSERT(send_trailing_metadata_storage_ == nullptr);
    grpc_metadata_batch* send_trailing_metadata =
        batch->payload->send_trailing_metadata.send_trailing_metadata;
    send_trailing_metadata_storage_ = (grpc_linked_mdelem*)arena_->Alloc(
        sizeof(grpc_linked_mdelem) * send_trailing_metadata->list.count);
    grpc_metadata_batch_copy(send_trailing_metadata, &send_trailing_metadata_,
                             send_trailing_metadata_storage_);
  }
}

void CallData::FreeCachedSendInitialMetadata(ChannelData* chand) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_initial_metadata", chand,
            this);
  }
  grpc_metadata_batch_destroy(&send_initial_metadata_);
}

void CallData::FreeCachedSendMessage(ChannelData* chand, size_t idx) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_messages[%" PRIuPTR "]",
            chand, this, idx);
  }
  send_messages_[idx]->Destroy();
}

void CallData::FreeCachedSendTrailingMetadata(ChannelData* chand) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: destroying calld->send_trailing_metadata",
            chand, this);
  }
  grpc_metadata_batch_destroy(&send_trailing_metadata_);
}

void CallData::FreeCachedSendOpDataAfterCommit(
    grpc_call_element* elem, SubchannelCallRetryState* retry_state) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (retry_state->completed_send_initial_metadata) {
    FreeCachedSendInitialMetadata(chand);
  }
  for (size_t i = 0; i < retry_state->completed_send_message_count; ++i) {
    FreeCachedSendMessage(chand, i);
  }
  if (retry_state->completed_send_trailing_metadata) {
    FreeCachedSendTrailingMetadata(chand);
  }
}

void CallData::FreeCachedSendOpDataForCompletedBatch(
    grpc_call_element* elem, SubchannelCallBatchData* batch_data,
    SubchannelCallRetryState* retry_state) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (batch_data->batch.send_initial_metadata) {
    FreeCachedSendInitialMetadata(chand);
  }
  if (batch_data->batch.send_message) {
    FreeCachedSendMessage(chand, retry_state->completed_send_message_count - 1);
  }
  if (batch_data->batch.send_trailing_metadata) {
    FreeCachedSendTrailingMetadata(chand);
  }
}

//
// LB recv_trailing_metadata_ready handling
//

void CallData::RecvTrailingMetadataReadyForLoadBalancingPolicy(
    void* arg, grpc_error* error) {
  CallData* calld = static_cast<CallData*>(arg);
  // Set error if call did not succeed.
  grpc_error* error_for_lb = GRPC_ERROR_NONE;
  if (error != GRPC_ERROR_NONE) {
    error_for_lb = error;
  } else {
    const auto& fields = calld->recv_trailing_metadata_->idx.named;
    GPR_ASSERT(fields.grpc_status != nullptr);
    grpc_status_code status =
        grpc_get_status_code_from_metadata(fields.grpc_status->md);
    std::string msg;
    if (status != GRPC_STATUS_OK) {
      error_for_lb = grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("call failed"),
          GRPC_ERROR_INT_GRPC_STATUS, status);
      if (fields.grpc_message != nullptr) {
        error_for_lb = grpc_error_set_str(
            error_for_lb, GRPC_ERROR_STR_GRPC_MESSAGE,
            grpc_slice_ref_internal(GRPC_MDVALUE(fields.grpc_message->md)));
      }
    }
  }
  // Invoke callback to LB policy.
  Metadata trailing_metadata(calld, calld->recv_trailing_metadata_);
  calld->lb_recv_trailing_metadata_ready_(error_for_lb, &trailing_metadata,
                                          &calld->lb_call_state_);
  if (error == GRPC_ERROR_NONE) GRPC_ERROR_UNREF(error_for_lb);
  // Chain to original callback.
  Closure::Run(DEBUG_LOCATION, calld->original_recv_trailing_metadata_ready_,
               GRPC_ERROR_REF(error));
}

void CallData::MaybeInjectRecvTrailingMetadataReadyForLoadBalancingPolicy(
    grpc_transport_stream_op_batch* batch) {
  if (lb_recv_trailing_metadata_ready_ != nullptr) {
    recv_trailing_metadata_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata;
    original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                      RecvTrailingMetadataReadyForLoadBalancingPolicy, this,
                      grpc_schedule_on_exec_ctx);
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }
}

//
// pending_batches management
//

size_t CallData::GetBatchIndex(grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in pick_subchannel_locked() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
void CallData::PendingBatchesAdd(grpc_call_element* elem,
                                 grpc_transport_stream_op_batch* batch) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: adding pending batch at index %" PRIuPTR, chand,
            this, idx);
  }
  PendingBatch* pending = &pending_batches_[idx];
  GPR_ASSERT(pending->batch == nullptr);
  pending->batch = batch;
  pending->send_ops_cached = false;
  if (enable_retries_) {
    // Update state in calld about pending batches.
    // Also check if the batch takes us over the retry buffer limit.
    // Note: We don't check the size of trailing metadata here, because
    // gRPC clients do not send trailing metadata.
    if (batch->send_initial_metadata) {
      pending_send_initial_metadata_ = true;
      bytes_buffered_for_retry_ += grpc_metadata_batch_size(
          batch->payload->send_initial_metadata.send_initial_metadata);
    }
    if (batch->send_message) {
      pending_send_message_ = true;
      bytes_buffered_for_retry_ +=
          batch->payload->send_message.send_message->length();
    }
    if (batch->send_trailing_metadata) {
      pending_send_trailing_metadata_ = true;
    }
    if (GPR_UNLIKELY(bytes_buffered_for_retry_ >
                     chand->per_rpc_retry_buffer_size())) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: exceeded retry buffer size, committing",
                chand, this);
      }
      SubchannelCallRetryState* retry_state =
          subchannel_call_ == nullptr ? nullptr
                                      : static_cast<SubchannelCallRetryState*>(
                                            subchannel_call_->GetParentData());
      RetryCommit(elem, retry_state);
      // If we are not going to retry and have not yet started, pretend
      // retries are disabled so that we don't bother with retry overhead.
      if (num_attempts_completed_ == 0) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
          gpr_log(GPR_INFO,
                  "chand=%p calld=%p: disabling retries before first attempt",
                  chand, this);
        }
        enable_retries_ = false;
      }
    }
  }
}

void CallData::PendingBatchClear(PendingBatch* pending) {
  if (enable_retries_) {
    if (pending->batch->send_initial_metadata) {
      pending_send_initial_metadata_ = false;
    }
    if (pending->batch->send_message) {
      pending_send_message_ = false;
    }
    if (pending->batch->send_trailing_metadata) {
      pending_send_trailing_metadata_ = false;
    }
  }
  pending->batch = nullptr;
}

void CallData::MaybeClearPendingBatch(grpc_call_element* elem,
                                      PendingBatch* pending) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  grpc_transport_stream_op_batch* batch = pending->batch;
  // We clear the pending batch if all of its callbacks have been
  // scheduled and reset to nullptr.
  if (batch->on_complete == nullptr &&
      (!batch->recv_initial_metadata ||
       batch->payload->recv_initial_metadata.recv_initial_metadata_ready ==
           nullptr) &&
      (!batch->recv_message ||
       batch->payload->recv_message.recv_message_ready == nullptr) &&
      (!batch->recv_trailing_metadata ||
       batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready ==
           nullptr)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: clearing pending batch", chand,
              this);
    }
    PendingBatchClear(pending);
  }
}

// This is called via the call combiner, so access to calld is synchronized.
void CallData::FailPendingBatchInCallCombiner(void* arg, grpc_error* error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  CallData* calld = static_cast<CallData*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(
      batch, GRPC_ERROR_REF(error), calld->call_combiner_);
}

// This is called via the call combiner, so access to calld is synchronized.
void CallData::PendingBatchesFail(
    grpc_call_element* elem, grpc_error* error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            elem->channel_data, this, num_batches, grpc_error_string(error));
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      if (batch->recv_trailing_metadata) {
        MaybeInjectRecvTrailingMetadataReadyForLoadBalancingPolicy(batch);
      }
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        FailPendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_REF(error),
                   "PendingBatchesFail");
      PendingBatchClear(pending);
    }
  }
  if (yield_call_combiner_predicate(closures)) {
    closures.RunClosures(call_combiner_);
  } else {
    closures.RunClosuresWithoutYielding(call_combiner_);
  }
  GRPC_ERROR_UNREF(error);
}

// This is called via the call combiner, so access to calld is synchronized.
void CallData::ResumePendingBatchInCallCombiner(void* arg,
                                                grpc_error* /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  SubchannelCall* subchannel_call =
      static_cast<SubchannelCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  subchannel_call->StartTransportStreamOpBatch(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
void CallData::PendingBatchesResume(grpc_call_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (enable_retries_) {
    StartRetriableSubchannelBatches(elem, GRPC_ERROR_NONE);
    return;
  }
  // Retries not enabled; send down batches as-is.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i].batch != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " pending batches on subchannel_call=%p",
            chand, this, num_batches, subchannel_call_.get());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr) {
      if (batch->recv_trailing_metadata) {
        MaybeInjectRecvTrailingMetadataReadyForLoadBalancingPolicy(batch);
      }
      batch->handler_private.extra_arg = subchannel_call_.get();
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        ResumePendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                   "PendingBatchesResume");
      PendingBatchClear(pending);
    }
  }
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner_);
}

template <typename Predicate>
CallData::PendingBatch* CallData::PendingBatchFind(grpc_call_element* elem,
                                                   const char* log_message,
                                                   Predicate predicate) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch != nullptr && predicate(batch)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: %s pending batch at index %" PRIuPTR, chand,
                this, log_message, i);
      }
      return pending;
    }
  }
  return nullptr;
}

//
// retry code
//

void CallData::RetryCommit(grpc_call_element* elem,
                           SubchannelCallRetryState* retry_state) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (retry_committed_) return;
  retry_committed_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: committing retries", chand, this);
  }
  if (retry_state != nullptr) {
    FreeCachedSendOpDataAfterCommit(elem, retry_state);
  }
}

void CallData::DoRetry(grpc_call_element* elem,
                       SubchannelCallRetryState* retry_state,
                       grpc_millis server_pushback_ms) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  GPR_ASSERT(method_params_ != nullptr);
  const auto* retry_policy = method_params_->retry_policy();
  GPR_ASSERT(retry_policy != nullptr);
  // Reset subchannel call.
  subchannel_call_.reset();
  // Compute backoff delay.
  grpc_millis next_attempt_time;
  if (server_pushback_ms >= 0) {
    next_attempt_time = ExecCtx::Get()->Now() + server_pushback_ms;
    last_attempt_got_server_pushback_ = true;
  } else {
    if (num_attempts_completed_ == 1 || last_attempt_got_server_pushback_) {
      retry_backoff_.Init(
          BackOff::Options()
              .set_initial_backoff(retry_policy->initial_backoff)
              .set_multiplier(retry_policy->backoff_multiplier)
              .set_jitter(RETRY_BACKOFF_JITTER)
              .set_max_backoff(retry_policy->max_backoff));
      last_attempt_got_server_pushback_ = false;
    }
    next_attempt_time = retry_backoff_->NextAttemptTime();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: retrying failed call in %" PRId64 " ms", chand,
            this, next_attempt_time - ExecCtx::Get()->Now());
  }
  // Schedule retry after computed delay.
  GRPC_CLOSURE_INIT(&pick_closure_, PickSubchannel, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&retry_timer_, next_attempt_time, &pick_closure_);
  // Update bookkeeping.
  if (retry_state != nullptr) retry_state->retry_dispatched = true;
}

bool CallData::MaybeRetry(grpc_call_element* elem,
                          SubchannelCallBatchData* batch_data,
                          grpc_status_code status,
                          grpc_mdelem* server_pushback_md) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  // Get retry policy.
  if (method_params_ == nullptr) return false;
  const auto* retry_policy = method_params_->retry_policy();
  if (retry_policy == nullptr) return false;
  // If we've already dispatched a retry from this call, return true.
  // This catches the case where the batch has multiple callbacks
  // (i.e., it includes either recv_message or recv_initial_metadata).
  SubchannelCallRetryState* retry_state = nullptr;
  if (batch_data != nullptr) {
    retry_state = static_cast<SubchannelCallRetryState*>(
        batch_data->subchannel_call->GetParentData());
    if (retry_state->retry_dispatched) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: retry already dispatched", chand,
                this);
      }
      return true;
    }
  }
  // Check status.
  if (GPR_LIKELY(status == GRPC_STATUS_OK)) {
    if (retry_throttle_data_ != nullptr) {
      retry_throttle_data_->RecordSuccess();
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: call succeeded", chand, this);
    }
    return false;
  }
  // Status is not OK.  Check whether the status is retryable.
  if (!retry_policy->retryable_status_codes.Contains(status)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: status %s not configured as retryable", chand,
              this, grpc_status_code_to_string(status));
    }
    return false;
  }
  // Record the failure and check whether retries are throttled.
  // Note that it's important for this check to come after the status
  // code check above, since we should only record failures whose statuses
  // match the configured retryable status codes, so that we don't count
  // things like failures due to malformed requests (INVALID_ARGUMENT).
  // Conversely, it's important for this to come before the remaining
  // checks, so that we don't fail to record failures due to other factors.
  if (retry_throttle_data_ != nullptr &&
      !retry_throttle_data_->RecordFailure()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries throttled", chand, this);
    }
    return false;
  }
  // Check whether the call is committed.
  if (retry_committed_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: retries already committed", chand,
              this);
    }
    return false;
  }
  // Check whether we have retries remaining.
  ++num_attempts_completed_;
  if (num_attempts_completed_ >= retry_policy->max_attempts) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: exceeded %d retry attempts", chand,
              this, retry_policy->max_attempts);
    }
    return false;
  }
  // If the call was cancelled from the surface, don't retry.
  if (cancel_error_ != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: call cancelled from surface, not retrying",
              chand, this);
    }
    return false;
  }
  // Check server push-back.
  grpc_millis server_pushback_ms = -1;
  if (server_pushback_md != nullptr) {
    // If the value is "-1" or any other unparseable string, we do not retry.
    uint32_t ms;
    if (!grpc_parse_slice_to_uint32(GRPC_MDVALUE(*server_pushback_md), &ms)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: not retrying due to server push-back",
                chand, this);
      }
      return false;
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: server push-back: retry in %u ms",
                chand, this, ms);
      }
      server_pushback_ms = (grpc_millis)ms;
    }
  }
  DoRetry(elem, retry_state, server_pushback_ms);
  return true;
}

//
// CallData::SubchannelCallBatchData
//

CallData::SubchannelCallBatchData* CallData::SubchannelCallBatchData::Create(
    grpc_call_element* elem, int refcount, bool set_on_complete) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  return calld->arena_->New<SubchannelCallBatchData>(elem, calld, refcount,
                                                     set_on_complete);
}

CallData::SubchannelCallBatchData::SubchannelCallBatchData(
    grpc_call_element* elem, CallData* calld, int refcount,
    bool set_on_complete)
    : elem(elem), subchannel_call(calld->subchannel_call_) {
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          calld->subchannel_call_->GetParentData());
  batch.payload = &retry_state->batch_payload;
  gpr_ref_init(&refs, refcount);
  if (set_on_complete) {
    GRPC_CLOSURE_INIT(&on_complete, CallData::OnComplete, this,
                      grpc_schedule_on_exec_ctx);
    batch.on_complete = &on_complete;
  }
  GRPC_CALL_STACK_REF(calld->owning_call_, "batch_data");
}

void CallData::SubchannelCallBatchData::Destroy() {
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(subchannel_call->GetParentData());
  if (batch.send_initial_metadata) {
    grpc_metadata_batch_destroy(&retry_state->send_initial_metadata);
  }
  if (batch.send_trailing_metadata) {
    grpc_metadata_batch_destroy(&retry_state->send_trailing_metadata);
  }
  if (batch.recv_initial_metadata) {
    grpc_metadata_batch_destroy(&retry_state->recv_initial_metadata);
  }
  if (batch.recv_trailing_metadata) {
    grpc_metadata_batch_destroy(&retry_state->recv_trailing_metadata);
  }
  subchannel_call.reset();
  CallData* calld = static_cast<CallData*>(elem->call_data);
  GRPC_CALL_STACK_UNREF(calld->owning_call_, "batch_data");
}

//
// recv_initial_metadata callback handling
//

void CallData::InvokeRecvInitialMetadataCallback(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  CallData* calld = static_cast<CallData*>(batch_data->elem->call_data);
  // Find pending batch.
  PendingBatch* pending = calld->PendingBatchFind(
      batch_data->elem, "invoking recv_initial_metadata_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_initial_metadata &&
               batch->payload->recv_initial_metadata
                       .recv_initial_metadata_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return metadata.
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  grpc_metadata_batch_move(
      &retry_state->recv_initial_metadata,
      pending->batch->payload->recv_initial_metadata.recv_initial_metadata);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_initial_metadata_ready =
      pending->batch->payload->recv_initial_metadata
          .recv_initial_metadata_ready;
  pending->batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
      nullptr;
  calld->MaybeClearPendingBatch(batch_data->elem, pending);
  batch_data->Unref();
  // Invoke callback.
  Closure::Run(DEBUG_LOCATION, recv_initial_metadata_ready,
               GRPC_ERROR_REF(error));
}

void CallData::RecvInitialMetadataReady(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  grpc_call_element* elem = batch_data->elem;
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_initial_metadata_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  retry_state->completed_recv_initial_metadata = true;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_initial_metadata op, so do nothing.
  if (retry_state->retry_dispatched) {
    GRPC_CALL_COMBINER_STOP(
        calld->call_combiner_,
        "recv_initial_metadata_ready after retry dispatched");
    return;
  }
  // If we got an error or a Trailers-Only response and have not yet gotten
  // the recv_trailing_metadata_ready callback, then defer propagating this
  // callback back to the surface.  We can evaluate whether to retry when
  // recv_trailing_metadata comes back.
  if (GPR_UNLIKELY((retry_state->trailing_metadata_available ||
                    error != GRPC_ERROR_NONE) &&
                   !retry_state->completed_recv_trailing_metadata)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: deferring recv_initial_metadata_ready "
              "(Trailers-Only)",
              chand, calld);
    }
    retry_state->recv_initial_metadata_ready_deferred_batch = batch_data;
    retry_state->recv_initial_metadata_error = GRPC_ERROR_REF(error);
    if (!retry_state->started_recv_trailing_metadata) {
      // recv_trailing_metadata not yet started by application; start it
      // ourselves to get status.
      calld->StartInternalRecvTrailingMetadata(elem);
    } else {
      GRPC_CALL_COMBINER_STOP(
          calld->call_combiner_,
          "recv_initial_metadata_ready trailers-only or error");
    }
    return;
  }
  // Received valid initial metadata, so commit the call.
  calld->RetryCommit(elem, retry_state);
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  calld->InvokeRecvInitialMetadataCallback(batch_data, error);
}

//
// recv_message callback handling
//

void CallData::InvokeRecvMessageCallback(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  CallData* calld = static_cast<CallData*>(batch_data->elem->call_data);
  // Find pending op.
  PendingBatch* pending = calld->PendingBatchFind(
      batch_data->elem, "invoking recv_message_ready for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_message &&
               batch->payload->recv_message.recv_message_ready != nullptr;
      });
  GPR_ASSERT(pending != nullptr);
  // Return payload.
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  *pending->batch->payload->recv_message.recv_message =
      std::move(retry_state->recv_message);
  // Update bookkeeping.
  // Note: Need to do this before invoking the callback, since invoking
  // the callback will result in yielding the call combiner.
  grpc_closure* recv_message_ready =
      pending->batch->payload->recv_message.recv_message_ready;
  pending->batch->payload->recv_message.recv_message_ready = nullptr;
  calld->MaybeClearPendingBatch(batch_data->elem, pending);
  batch_data->Unref();
  // Invoke callback.
  Closure::Run(DEBUG_LOCATION, recv_message_ready, GRPC_ERROR_REF(error));
}

void CallData::RecvMessageReady(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  grpc_call_element* elem = batch_data->elem;
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got recv_message_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  ++retry_state->completed_recv_message_count;
  // If a retry was already dispatched, then we're not going to use the
  // result of this recv_message op, so do nothing.
  if (retry_state->retry_dispatched) {
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "recv_message_ready after retry dispatched");
    return;
  }
  // If we got an error or the payload was nullptr and we have not yet gotten
  // the recv_trailing_metadata_ready callback, then defer propagating this
  // callback back to the surface.  We can evaluate whether to retry when
  // recv_trailing_metadata comes back.
  if (GPR_UNLIKELY(
          (retry_state->recv_message == nullptr || error != GRPC_ERROR_NONE) &&
          !retry_state->completed_recv_trailing_metadata)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: deferring recv_message_ready (nullptr "
              "message and recv_trailing_metadata pending)",
              chand, calld);
    }
    retry_state->recv_message_ready_deferred_batch = batch_data;
    retry_state->recv_message_error = GRPC_ERROR_REF(error);
    if (!retry_state->started_recv_trailing_metadata) {
      // recv_trailing_metadata not yet started by application; start it
      // ourselves to get status.
      calld->StartInternalRecvTrailingMetadata(elem);
    } else {
      GRPC_CALL_COMBINER_STOP(calld->call_combiner_, "recv_message_ready null");
    }
    return;
  }
  // Received a valid message, so commit the call.
  calld->RetryCommit(elem, retry_state);
  // Invoke the callback to return the result to the surface.
  // Manually invoking a callback function; it does not take ownership of error.
  calld->InvokeRecvMessageCallback(batch_data, error);
}

//
// recv_trailing_metadata handling
//

void CallData::GetCallStatus(grpc_metadata_batch* md_batch, grpc_error* error,
                             grpc_status_code* status,
                             grpc_mdelem** server_pushback_md) {
  if (error != GRPC_ERROR_NONE) {
    grpc_error_get_status(error, deadline_, status, nullptr, nullptr, nullptr);
  } else {
    GPR_ASSERT(md_batch->idx.named.grpc_status != nullptr);
    *status =
        grpc_get_status_code_from_metadata(md_batch->idx.named.grpc_status->md);
    if (server_pushback_md != nullptr &&
        md_batch->idx.named.grpc_retry_pushback_ms != nullptr) {
      *server_pushback_md = &md_batch->idx.named.grpc_retry_pushback_ms->md;
    }
  }
  GRPC_ERROR_UNREF(error);
}

void CallData::AddClosureForRecvTrailingMetadataReady(
    grpc_call_element* elem, SubchannelCallBatchData* batch_data,
    grpc_error* error, CallCombinerClosureList* closures) {
  // Find pending batch.
  PendingBatch* pending = PendingBatchFind(
      elem, "invoking recv_trailing_metadata for",
      [](grpc_transport_stream_op_batch* batch) {
        return batch->recv_trailing_metadata &&
               batch->payload->recv_trailing_metadata
                       .recv_trailing_metadata_ready != nullptr;
      });
  // If we generated the recv_trailing_metadata op internally via
  // StartInternalRecvTrailingMetadata(), then there will be no pending batch.
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Return metadata.
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  grpc_metadata_batch_move(
      &retry_state->recv_trailing_metadata,
      pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata);
  // Add closure.
  closures->Add(pending->batch->payload->recv_trailing_metadata
                    .recv_trailing_metadata_ready,
                error, "recv_trailing_metadata_ready for pending batch");
  // Update bookkeeping.
  pending->batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      nullptr;
  MaybeClearPendingBatch(elem, pending);
}

void CallData::AddClosuresForDeferredRecvCallbacks(
    SubchannelCallBatchData* batch_data, SubchannelCallRetryState* retry_state,
    CallCombinerClosureList* closures) {
  if (batch_data->batch.recv_trailing_metadata) {
    // Add closure for deferred recv_initial_metadata_ready.
    if (GPR_UNLIKELY(retry_state->recv_initial_metadata_ready_deferred_batch !=
                     nullptr)) {
      GRPC_CLOSURE_INIT(&retry_state->recv_initial_metadata_ready,
                        InvokeRecvInitialMetadataCallback,
                        retry_state->recv_initial_metadata_ready_deferred_batch,
                        grpc_schedule_on_exec_ctx);
      closures->Add(&retry_state->recv_initial_metadata_ready,
                    retry_state->recv_initial_metadata_error,
                    "resuming recv_initial_metadata_ready");
      retry_state->recv_initial_metadata_ready_deferred_batch = nullptr;
    }
    // Add closure for deferred recv_message_ready.
    if (GPR_UNLIKELY(retry_state->recv_message_ready_deferred_batch !=
                     nullptr)) {
      GRPC_CLOSURE_INIT(&retry_state->recv_message_ready,
                        InvokeRecvMessageCallback,
                        retry_state->recv_message_ready_deferred_batch,
                        grpc_schedule_on_exec_ctx);
      closures->Add(&retry_state->recv_message_ready,
                    retry_state->recv_message_error,
                    "resuming recv_message_ready");
      retry_state->recv_message_ready_deferred_batch = nullptr;
    }
  }
}

bool CallData::PendingBatchIsUnstarted(PendingBatch* pending,
                                       SubchannelCallRetryState* retry_state) {
  if (pending->batch == nullptr || pending->batch->on_complete == nullptr) {
    return false;
  }
  if (pending->batch->send_initial_metadata &&
      !retry_state->started_send_initial_metadata) {
    return true;
  }
  if (pending->batch->send_message &&
      retry_state->started_send_message_count < send_messages_.size()) {
    return true;
  }
  if (pending->batch->send_trailing_metadata &&
      !retry_state->started_send_trailing_metadata) {
    return true;
  }
  return false;
}

void CallData::AddClosuresToFailUnstartedPendingBatches(
    grpc_call_element* elem, SubchannelCallRetryState* retry_state,
    grpc_error* error, CallCombinerClosureList* closures) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    if (PendingBatchIsUnstarted(pending, retry_state)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: failing unstarted pending batch at index "
                "%" PRIuPTR,
                chand, this, i);
      }
      closures->Add(pending->batch->on_complete, GRPC_ERROR_REF(error),
                    "failing on_complete for pending batch");
      pending->batch->on_complete = nullptr;
      MaybeClearPendingBatch(elem, pending);
    }
  }
  GRPC_ERROR_UNREF(error);
}

void CallData::RunClosuresForCompletedCall(SubchannelCallBatchData* batch_data,
                                           grpc_error* error) {
  grpc_call_element* elem = batch_data->elem;
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  // Construct list of closures to execute.
  CallCombinerClosureList closures;
  // First, add closure for recv_trailing_metadata_ready.
  AddClosureForRecvTrailingMetadataReady(elem, batch_data,
                                         GRPC_ERROR_REF(error), &closures);
  // If there are deferred recv_initial_metadata_ready or recv_message_ready
  // callbacks, add them to closures.
  AddClosuresForDeferredRecvCallbacks(batch_data, retry_state, &closures);
  // Add closures to fail any pending batches that have not yet been started.
  AddClosuresToFailUnstartedPendingBatches(elem, retry_state,
                                           GRPC_ERROR_REF(error), &closures);
  // Don't need batch_data anymore.
  batch_data->Unref();
  // Schedule all of the closures identified above.
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner_);
  GRPC_ERROR_UNREF(error);
}

void CallData::RecvTrailingMetadataReady(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  grpc_call_element* elem = batch_data->elem;
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_trailing_metadata_ready, error=%s",
            chand, calld, grpc_error_string(error));
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  retry_state->completed_recv_trailing_metadata = true;
  // Get the call's status and check for server pushback metadata.
  grpc_status_code status = GRPC_STATUS_OK;
  grpc_mdelem* server_pushback_md = nullptr;
  grpc_metadata_batch* md_batch =
      batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata;
  calld->GetCallStatus(md_batch, GRPC_ERROR_REF(error), &status,
                       &server_pushback_md);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: call finished, status=%s", chand,
            calld, grpc_status_code_to_string(status));
  }
  // Check if we should retry.
  if (calld->MaybeRetry(elem, batch_data, status, server_pushback_md)) {
    // Unref batch_data for deferred recv_initial_metadata_ready or
    // recv_message_ready callbacks, if any.
    if (retry_state->recv_initial_metadata_ready_deferred_batch != nullptr) {
      batch_data->Unref();
      GRPC_ERROR_UNREF(retry_state->recv_initial_metadata_error);
    }
    if (retry_state->recv_message_ready_deferred_batch != nullptr) {
      batch_data->Unref();
      GRPC_ERROR_UNREF(retry_state->recv_message_error);
    }
    batch_data->Unref();
    return;
  }
  // Not retrying, so commit the call.
  calld->RetryCommit(elem, retry_state);
  // Run any necessary closures.
  calld->RunClosuresForCompletedCall(batch_data, GRPC_ERROR_REF(error));
}

//
// on_complete callback handling
//

void CallData::AddClosuresForCompletedPendingBatch(
    grpc_call_element* elem, SubchannelCallBatchData* batch_data,
    grpc_error* error, CallCombinerClosureList* closures) {
  PendingBatch* pending = PendingBatchFind(
      elem, "completed", [batch_data](grpc_transport_stream_op_batch* batch) {
        // Match the pending batch with the same set of send ops as the
        // subchannel batch we've just completed.
        return batch->on_complete != nullptr &&
               batch_data->batch.send_initial_metadata ==
                   batch->send_initial_metadata &&
               batch_data->batch.send_message == batch->send_message &&
               batch_data->batch.send_trailing_metadata ==
                   batch->send_trailing_metadata;
      });
  // If batch_data is a replay batch, then there will be no pending
  // batch to complete.
  if (pending == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  // Add closure.
  closures->Add(pending->batch->on_complete, error,
                "on_complete for pending batch");
  pending->batch->on_complete = nullptr;
  MaybeClearPendingBatch(elem, pending);
}

void CallData::AddClosuresForReplayOrPendingSendOps(
    grpc_call_element* elem, SubchannelCallBatchData* batch_data,
    SubchannelCallRetryState* retry_state, CallCombinerClosureList* closures) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  bool have_pending_send_message_ops =
      retry_state->started_send_message_count < send_messages_.size();
  bool have_pending_send_trailing_metadata_op =
      seen_send_trailing_metadata_ &&
      !retry_state->started_send_trailing_metadata;
  if (!have_pending_send_message_ops &&
      !have_pending_send_trailing_metadata_op) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      PendingBatch* pending = &pending_batches_[i];
      grpc_transport_stream_op_batch* batch = pending->batch;
      if (batch == nullptr || pending->send_ops_cached) continue;
      if (batch->send_message) have_pending_send_message_ops = true;
      if (batch->send_trailing_metadata) {
        have_pending_send_trailing_metadata_op = true;
      }
    }
  }
  if (have_pending_send_message_ops || have_pending_send_trailing_metadata_op) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: starting next batch for pending send op(s)",
              chand, this);
    }
    GRPC_CLOSURE_INIT(&batch_data->batch.handler_private.closure,
                      StartRetriableSubchannelBatches, elem,
                      grpc_schedule_on_exec_ctx);
    closures->Add(&batch_data->batch.handler_private.closure, GRPC_ERROR_NONE,
                  "starting next batch for send_* op(s)");
  }
}

void CallData::OnComplete(void* arg, grpc_error* error) {
  SubchannelCallBatchData* batch_data =
      static_cast<SubchannelCallBatchData*>(arg);
  grpc_call_element* elem = batch_data->elem;
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: got on_complete, error=%s, batch=%s",
            chand, calld, grpc_error_string(error),
            grpc_transport_stream_op_batch_string(&batch_data->batch).c_str());
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          batch_data->subchannel_call->GetParentData());
  // Update bookkeeping in retry_state.
  if (batch_data->batch.send_initial_metadata) {
    retry_state->completed_send_initial_metadata = true;
  }
  if (batch_data->batch.send_message) {
    ++retry_state->completed_send_message_count;
  }
  if (batch_data->batch.send_trailing_metadata) {
    retry_state->completed_send_trailing_metadata = true;
  }
  // If the call is committed, free cached data for send ops that we've just
  // completed.
  if (calld->retry_committed_) {
    calld->FreeCachedSendOpDataForCompletedBatch(elem, batch_data, retry_state);
  }
  // Construct list of closures to execute.
  CallCombinerClosureList closures;
  // If a retry was already dispatched, that means we saw
  // recv_trailing_metadata before this, so we do nothing here.
  // Otherwise, invoke the callback to return the result to the surface.
  if (!retry_state->retry_dispatched) {
    // Add closure for the completed pending batch, if any.
    calld->AddClosuresForCompletedPendingBatch(
        elem, batch_data, GRPC_ERROR_REF(error), &closures);
    // If needed, add a callback to start any replay or pending send ops on
    // the subchannel call.
    if (!retry_state->completed_recv_trailing_metadata) {
      calld->AddClosuresForReplayOrPendingSendOps(elem, batch_data, retry_state,
                                                  &closures);
    }
  }
  // Track number of pending subchannel send batches and determine if this
  // was the last one.
  --calld->num_pending_retriable_subchannel_send_batches_;
  const bool last_send_batch_complete =
      calld->num_pending_retriable_subchannel_send_batches_ == 0;
  // Don't need batch_data anymore.
  batch_data->Unref();
  // Schedule all of the closures identified above.
  // Note: This yeilds the call combiner.
  closures.RunClosures(calld->call_combiner_);
  // If this was the last subchannel send batch, unref the call stack.
  if (last_send_batch_complete) {
    GRPC_CALL_STACK_UNREF(calld->owning_call_, "subchannel_send_batches");
  }
}

//
// subchannel batch construction
//

void CallData::StartBatchInCallCombiner(void* arg, grpc_error* /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  SubchannelCall* subchannel_call =
      static_cast<SubchannelCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  subchannel_call->StartTransportStreamOpBatch(batch);
}

void CallData::AddClosureForSubchannelBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch,
    CallCombinerClosureList* closures) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  batch->handler_private.extra_arg = subchannel_call_.get();
  GRPC_CLOSURE_INIT(&batch->handler_private.closure, StartBatchInCallCombiner,
                    batch, grpc_schedule_on_exec_ctx);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: starting subchannel batch: %s", chand,
            this, grpc_transport_stream_op_batch_string(batch).c_str());
  }
  closures->Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                "start_subchannel_batch");
}

void CallData::AddRetriableSendInitialMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  // Maps the number of retries to the corresponding metadata value slice.
  const grpc_slice* retry_count_strings[] = {&GRPC_MDSTR_1, &GRPC_MDSTR_2,
                                             &GRPC_MDSTR_3, &GRPC_MDSTR_4};
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  //
  // If we've already completed one or more attempts, add the
  // grpc-retry-attempts header.
  retry_state->send_initial_metadata_storage =
      static_cast<grpc_linked_mdelem*>(arena_->Alloc(
          sizeof(grpc_linked_mdelem) *
          (send_initial_metadata_.list.count + (num_attempts_completed_ > 0))));
  grpc_metadata_batch_copy(&send_initial_metadata_,
                           &retry_state->send_initial_metadata,
                           retry_state->send_initial_metadata_storage);
  if (GPR_UNLIKELY(retry_state->send_initial_metadata.idx.named
                       .grpc_previous_rpc_attempts != nullptr)) {
    grpc_metadata_batch_remove(&retry_state->send_initial_metadata,
                               GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS);
  }
  if (GPR_UNLIKELY(num_attempts_completed_ > 0)) {
    grpc_mdelem retry_md = grpc_mdelem_create(
        GRPC_MDSTR_GRPC_PREVIOUS_RPC_ATTEMPTS,
        *retry_count_strings[num_attempts_completed_ - 1], nullptr);
    grpc_error* error = grpc_metadata_batch_add_tail(
        &retry_state->send_initial_metadata,
        &retry_state
             ->send_initial_metadata_storage[send_initial_metadata_.list.count],
        retry_md, GRPC_BATCH_GRPC_PREVIOUS_RPC_ATTEMPTS);
    if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
      gpr_log(GPR_ERROR, "error adding retry metadata: %s",
              grpc_error_string(error));
      GPR_ASSERT(false);
    }
  }
  retry_state->started_send_initial_metadata = true;
  batch_data->batch.send_initial_metadata = true;
  batch_data->batch.payload->send_initial_metadata.send_initial_metadata =
      &retry_state->send_initial_metadata;
  batch_data->batch.payload->send_initial_metadata.send_initial_metadata_flags =
      send_initial_metadata_flags_;
  batch_data->batch.payload->send_initial_metadata.peer_string = peer_string_;
}

void CallData::AddRetriableSendMessageOp(grpc_call_element* elem,
                                         SubchannelCallRetryState* retry_state,
                                         SubchannelCallBatchData* batch_data) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting calld->send_messages[%" PRIuPTR "]",
            chand, this, retry_state->started_send_message_count);
  }
  ByteStreamCache* cache =
      send_messages_[retry_state->started_send_message_count];
  ++retry_state->started_send_message_count;
  retry_state->send_message.Init(cache);
  batch_data->batch.send_message = true;
  batch_data->batch.payload->send_message.send_message.reset(
      retry_state->send_message.get());
}

void CallData::AddRetriableSendTrailingMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  // We need to make a copy of the metadata batch for each attempt, since
  // the filters in the subchannel stack may modify this batch, and we don't
  // want those modifications to be passed forward to subsequent attempts.
  retry_state->send_trailing_metadata_storage =
      static_cast<grpc_linked_mdelem*>(arena_->Alloc(
          sizeof(grpc_linked_mdelem) * send_trailing_metadata_.list.count));
  grpc_metadata_batch_copy(&send_trailing_metadata_,
                           &retry_state->send_trailing_metadata,
                           retry_state->send_trailing_metadata_storage);
  retry_state->started_send_trailing_metadata = true;
  batch_data->batch.send_trailing_metadata = true;
  batch_data->batch.payload->send_trailing_metadata.send_trailing_metadata =
      &retry_state->send_trailing_metadata;
}

void CallData::AddRetriableRecvInitialMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  retry_state->started_recv_initial_metadata = true;
  batch_data->batch.recv_initial_metadata = true;
  grpc_metadata_batch_init(&retry_state->recv_initial_metadata);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata =
      &retry_state->recv_initial_metadata;
  batch_data->batch.payload->recv_initial_metadata.trailing_metadata_available =
      &retry_state->trailing_metadata_available;
  GRPC_CLOSURE_INIT(&retry_state->recv_initial_metadata_ready,
                    RecvInitialMetadataReady, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_initial_metadata.recv_initial_metadata_ready =
      &retry_state->recv_initial_metadata_ready;
}

void CallData::AddRetriableRecvMessageOp(SubchannelCallRetryState* retry_state,
                                         SubchannelCallBatchData* batch_data) {
  ++retry_state->started_recv_message_count;
  batch_data->batch.recv_message = true;
  batch_data->batch.payload->recv_message.recv_message =
      &retry_state->recv_message;
  GRPC_CLOSURE_INIT(&retry_state->recv_message_ready, RecvMessageReady,
                    batch_data, grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_message.recv_message_ready =
      &retry_state->recv_message_ready;
}

void CallData::AddRetriableRecvTrailingMetadataOp(
    SubchannelCallRetryState* retry_state,
    SubchannelCallBatchData* batch_data) {
  retry_state->started_recv_trailing_metadata = true;
  batch_data->batch.recv_trailing_metadata = true;
  grpc_metadata_batch_init(&retry_state->recv_trailing_metadata);
  batch_data->batch.payload->recv_trailing_metadata.recv_trailing_metadata =
      &retry_state->recv_trailing_metadata;
  batch_data->batch.payload->recv_trailing_metadata.collect_stats =
      &retry_state->collect_stats;
  GRPC_CLOSURE_INIT(&retry_state->recv_trailing_metadata_ready,
                    RecvTrailingMetadataReady, batch_data,
                    grpc_schedule_on_exec_ctx);
  batch_data->batch.payload->recv_trailing_metadata
      .recv_trailing_metadata_ready =
      &retry_state->recv_trailing_metadata_ready;
  MaybeInjectRecvTrailingMetadataReadyForLoadBalancingPolicy(
      &batch_data->batch);
}

void CallData::StartInternalRecvTrailingMetadata(grpc_call_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: call failed but recv_trailing_metadata not "
            "started; starting it internally",
            chand, this);
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(subchannel_call_->GetParentData());
  // Create batch_data with 2 refs, since this batch will be unreffed twice:
  // once for the recv_trailing_metadata_ready callback when the subchannel
  // batch returns, and again when we actually get a recv_trailing_metadata
  // op from the surface.
  SubchannelCallBatchData* batch_data =
      SubchannelCallBatchData::Create(elem, 2, false /* set_on_complete */);
  AddRetriableRecvTrailingMetadataOp(retry_state, batch_data);
  retry_state->recv_trailing_metadata_internal_batch = batch_data;
  // Note: This will release the call combiner.
  subchannel_call_->StartTransportStreamOpBatch(&batch_data->batch);
}

// If there are any cached send ops that need to be replayed on the
// current subchannel call, creates and returns a new subchannel batch
// to replay those ops.  Otherwise, returns nullptr.
CallData::SubchannelCallBatchData*
CallData::MaybeCreateSubchannelBatchForReplay(
    grpc_call_element* elem, SubchannelCallRetryState* retry_state) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  SubchannelCallBatchData* replay_batch_data = nullptr;
  // send_initial_metadata.
  if (seen_send_initial_metadata_ &&
      !retry_state->started_send_initial_metadata &&
      !pending_send_initial_metadata_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_initial_metadata op",
              chand, this);
    }
    replay_batch_data =
        SubchannelCallBatchData::Create(elem, 1, true /* set_on_complete */);
    AddRetriableSendInitialMetadataOp(retry_state, replay_batch_data);
  }
  // send_message.
  // Note that we can only have one send_message op in flight at a time.
  if (retry_state->started_send_message_count < send_messages_.size() &&
      retry_state->started_send_message_count ==
          retry_state->completed_send_message_count &&
      !pending_send_message_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_message op",
              chand, this);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data =
          SubchannelCallBatchData::Create(elem, 1, true /* set_on_complete */);
    }
    AddRetriableSendMessageOp(elem, retry_state, replay_batch_data);
  }
  // send_trailing_metadata.
  // Note that we only add this op if we have no more send_message ops
  // to start, since we can't send down any more send_message ops after
  // send_trailing_metadata.
  if (seen_send_trailing_metadata_ &&
      retry_state->started_send_message_count == send_messages_.size() &&
      !retry_state->started_send_trailing_metadata &&
      !pending_send_trailing_metadata_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: replaying previously completed "
              "send_trailing_metadata op",
              chand, this);
    }
    if (replay_batch_data == nullptr) {
      replay_batch_data =
          SubchannelCallBatchData::Create(elem, 1, true /* set_on_complete */);
    }
    AddRetriableSendTrailingMetadataOp(retry_state, replay_batch_data);
  }
  return replay_batch_data;
}

void CallData::AddSubchannelBatchesForPendingBatches(
    grpc_call_element* elem, SubchannelCallRetryState* retry_state,
    CallCombinerClosureList* closures) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    PendingBatch* pending = &pending_batches_[i];
    grpc_transport_stream_op_batch* batch = pending->batch;
    if (batch == nullptr) continue;
    // Skip any batch that either (a) has already been started on this
    // subchannel call or (b) we can't start yet because we're still
    // replaying send ops that need to be completed first.
    // TODO(roth): Note that if any one op in the batch can't be sent
    // yet due to ops that we're replaying, we don't start any of the ops
    // in the batch.  This is probably okay, but it could conceivably
    // lead to increased latency in some cases -- e.g., we could delay
    // starting a recv op due to it being in the same batch with a send
    // op.  If/when we revamp the callback protocol in
    // transport_stream_op_batch, we may be able to fix this.
    if (batch->send_initial_metadata &&
        retry_state->started_send_initial_metadata) {
      continue;
    }
    if (batch->send_message && retry_state->completed_send_message_count <
                                   retry_state->started_send_message_count) {
      continue;
    }
    // Note that we only start send_trailing_metadata if we have no more
    // send_message ops to start, since we can't send down any more
    // send_message ops after send_trailing_metadata.
    if (batch->send_trailing_metadata &&
        (retry_state->started_send_message_count + batch->send_message <
             send_messages_.size() ||
         retry_state->started_send_trailing_metadata)) {
      continue;
    }
    if (batch->recv_initial_metadata &&
        retry_state->started_recv_initial_metadata) {
      continue;
    }
    if (batch->recv_message && retry_state->completed_recv_message_count <
                                   retry_state->started_recv_message_count) {
      continue;
    }
    if (batch->recv_trailing_metadata &&
        retry_state->started_recv_trailing_metadata) {
      // If we previously completed a recv_trailing_metadata op
      // initiated by StartInternalRecvTrailingMetadata(), use the
      // result of that instead of trying to re-start this op.
      if (GPR_UNLIKELY((retry_state->recv_trailing_metadata_internal_batch !=
                        nullptr))) {
        // If the batch completed, then trigger the completion callback
        // directly, so that we return the previously returned results to
        // the application.  Otherwise, just unref the internally
        // started subchannel batch, since we'll propagate the
        // completion when it completes.
        if (retry_state->completed_recv_trailing_metadata) {
          // Batches containing recv_trailing_metadata always succeed.
          closures->Add(
              &retry_state->recv_trailing_metadata_ready, GRPC_ERROR_NONE,
              "re-executing recv_trailing_metadata_ready to propagate "
              "internally triggered result");
        } else {
          retry_state->recv_trailing_metadata_internal_batch->Unref();
        }
        retry_state->recv_trailing_metadata_internal_batch = nullptr;
      }
      continue;
    }
    // If we're not retrying, just send the batch as-is.
    if (method_params_ == nullptr ||
        method_params_->retry_policy() == nullptr || retry_committed_) {
      // TODO(roth) : We should probably call
      // MaybeInjectRecvTrailingMetadataReadyForLoadBalancingPolicy here.
      AddClosureForSubchannelBatch(elem, batch, closures);
      PendingBatchClear(pending);
      continue;
    }
    // Create batch with the right number of callbacks.
    const bool has_send_ops = batch->send_initial_metadata ||
                              batch->send_message ||
                              batch->send_trailing_metadata;
    const int num_callbacks = has_send_ops + batch->recv_initial_metadata +
                              batch->recv_message +
                              batch->recv_trailing_metadata;
    SubchannelCallBatchData* batch_data = SubchannelCallBatchData::Create(
        elem, num_callbacks, has_send_ops /* set_on_complete */);
    // Cache send ops if needed.
    MaybeCacheSendOpsForBatch(pending);
    // send_initial_metadata.
    if (batch->send_initial_metadata) {
      AddRetriableSendInitialMetadataOp(retry_state, batch_data);
    }
    // send_message.
    if (batch->send_message) {
      AddRetriableSendMessageOp(elem, retry_state, batch_data);
    }
    // send_trailing_metadata.
    if (batch->send_trailing_metadata) {
      AddRetriableSendTrailingMetadataOp(retry_state, batch_data);
    }
    // recv_initial_metadata.
    if (batch->recv_initial_metadata) {
      // recv_flags is only used on the server side.
      GPR_ASSERT(batch->payload->recv_initial_metadata.recv_flags == nullptr);
      AddRetriableRecvInitialMetadataOp(retry_state, batch_data);
    }
    // recv_message.
    if (batch->recv_message) {
      AddRetriableRecvMessageOp(retry_state, batch_data);
    }
    // recv_trailing_metadata.
    if (batch->recv_trailing_metadata) {
      AddRetriableRecvTrailingMetadataOp(retry_state, batch_data);
    }
    AddClosureForSubchannelBatch(elem, &batch_data->batch, closures);
    // Track number of pending subchannel send batches.
    // If this is the first one, take a ref to the call stack.
    if (batch->send_initial_metadata || batch->send_message ||
        batch->send_trailing_metadata) {
      if (num_pending_retriable_subchannel_send_batches_ == 0) {
        GRPC_CALL_STACK_REF(owning_call_, "subchannel_send_batches");
      }
      ++num_pending_retriable_subchannel_send_batches_;
    }
  }
}

void CallData::StartRetriableSubchannelBatches(void* arg,
                                               grpc_error* /*ignored*/) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: constructing retriable batches",
            chand, calld);
  }
  SubchannelCallRetryState* retry_state =
      static_cast<SubchannelCallRetryState*>(
          calld->subchannel_call_->GetParentData());
  // Construct list of closures to execute, one for each pending batch.
  CallCombinerClosureList closures;
  // Replay previously-returned send_* ops if needed.
  SubchannelCallBatchData* replay_batch_data =
      calld->MaybeCreateSubchannelBatchForReplay(elem, retry_state);
  if (replay_batch_data != nullptr) {
    calld->AddClosureForSubchannelBatch(elem, &replay_batch_data->batch,
                                        &closures);
    // Track number of pending subchannel send batches.
    // If this is the first one, take a ref to the call stack.
    if (calld->num_pending_retriable_subchannel_send_batches_ == 0) {
      GRPC_CALL_STACK_REF(calld->owning_call_, "subchannel_send_batches");
    }
    ++calld->num_pending_retriable_subchannel_send_batches_;
  }
  // Now add pending batches.
  calld->AddSubchannelBatchesForPendingBatches(elem, retry_state, &closures);
  // Start batches on subchannel call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " retriable batches on subchannel_call=%p",
            chand, calld, closures.size(), calld->subchannel_call_.get());
  }
  // Note: This will yield the call combiner.
  closures.RunClosures(calld->call_combiner_);
}

//
// LB pick
//

void CallData::CreateSubchannelCall(grpc_call_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  const size_t parent_data_size =
      enable_retries_ ? sizeof(SubchannelCallRetryState) : 0;
  SubchannelCall::Args call_args = {
      std::move(connected_subchannel_), pollent_, path_, call_start_time_,
      deadline_, arena_,
      // TODO(roth): When we implement hedging support, we will probably
      // need to use a separate call context for each subchannel call.
      call_context_, call_combiner_, parent_data_size};
  grpc_error* error = GRPC_ERROR_NONE;
  subchannel_call_ = SubchannelCall::Create(std::move(call_args), &error);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: create subchannel_call=%p: error=%s",
            chand, this, subchannel_call_.get(), grpc_error_string(error));
  }
  if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
    PendingBatchesFail(elem, error, YieldCallCombiner);
  } else {
    if (parent_data_size > 0) {
      new (subchannel_call_->GetParentData())
          SubchannelCallRetryState(call_context_);
    }
    PendingBatchesResume(elem);
  }
}

void CallData::AsyncPickDone(grpc_call_element* elem, grpc_error* error) {
  GRPC_CLOSURE_INIT(&pick_closure_, PickDone, elem, grpc_schedule_on_exec_ctx);
  ExecCtx::Run(DEBUG_LOCATION, &pick_closure_, error);
}

void CallData::PickDone(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: failed to pick subchannel: error=%s", chand,
              calld, grpc_error_string(error));
    }
    calld->PendingBatchesFail(elem, GRPC_ERROR_REF(error), YieldCallCombiner);
    return;
  }
  calld->CreateSubchannelCall(elem);
}

// A class to handle the call combiner cancellation callback for a
// queued pick.
class CallData::QueuedPickCanceller {
 public:
  explicit QueuedPickCanceller(grpc_call_element* elem) : elem_(elem) {
    auto* calld = static_cast<CallData*>(elem->call_data);
    GRPC_CALL_STACK_REF(calld->owning_call_, "QueuedPickCanceller");
    GRPC_CLOSURE_INIT(&closure_, &CancelLocked, this,
                      grpc_schedule_on_exec_ctx);
    calld->call_combiner_->SetNotifyOnCancel(&closure_);
  }

 private:
  static void CancelLocked(void* arg, grpc_error* error) {
    auto* self = static_cast<QueuedPickCanceller*>(arg);
    auto* chand = static_cast<ChannelData*>(self->elem_->channel_data);
    auto* calld = static_cast<CallData*>(self->elem_->call_data);
    MutexLock lock(chand->data_plane_mu());
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: cancelling queued pick: "
              "error=%s self=%p calld->pick_canceller=%p",
              chand, calld, grpc_error_string(error), self,
              calld->pick_canceller_);
    }
    if (calld->pick_canceller_ == self && error != GRPC_ERROR_NONE) {
      // Remove pick from list of queued picks.
      calld->RemoveCallFromQueuedPicksLocked(self->elem_);
      // Fail pending batches on the call.
      calld->PendingBatchesFail(self->elem_, GRPC_ERROR_REF(error),
                                YieldCallCombinerIfPendingBatchesFound);
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call_, "QueuedPickCanceller");
    delete self;
  }

  grpc_call_element* elem_;
  grpc_closure closure_;
};

void CallData::RemoveCallFromQueuedPicksLocked(grpc_call_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: removing from queued picks list",
            chand, this);
  }
  chand->RemoveQueuedPick(&pick_, pollent_);
  pick_queued_ = false;
  // Lame the call combiner canceller.
  pick_canceller_ = nullptr;
}

void CallData::AddCallToQueuedPicksLocked(grpc_call_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: adding to queued picks list", chand,
            this);
  }
  pick_queued_ = true;
  pick_.elem = elem;
  chand->AddQueuedPick(&pick_, pollent_);
  // Register call combiner cancellation callback.
  pick_canceller_ = new QueuedPickCanceller(elem);
}

void CallData::ApplyServiceConfigToCallLocked(grpc_call_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: applying service config to call",
            chand, this);
  }
  auto service_config = chand->service_config();
  if (service_config != nullptr) {
    // Create a ServiceConfigCallData for the call.  This stores a ref to the
    // ServiceConfig and caches the right set of parsed configs to use for
    // the call.  The MethodConfig will store itself in the call context,
    // so that it can be accessed by filters in the subchannel, and it
    // will be cleaned up when the call ends.
    const auto* method_params_vector =
        service_config->GetMethodParsedConfigVector(path_);
    auto* service_config_call_data = arena_->New<ServiceConfigCallData>(
        std::move(service_config), method_params_vector, call_context_);
    // Apply our own method params to the call.
    method_params_ = static_cast<ClientChannelMethodParsedConfig*>(
        service_config_call_data->GetMethodParsedConfig(
            internal::ClientChannelServiceConfigParser::ParserIndex()));
    if (method_params_ != nullptr) {
      // If the deadline from the service config is shorter than the one
      // from the client API, reset the deadline timer.
      if (chand->deadline_checking_enabled() &&
          method_params_->timeout() != 0) {
        const grpc_millis per_method_deadline =
            grpc_cycle_counter_to_millis_round_up(call_start_time_) +
            method_params_->timeout();
        if (per_method_deadline < deadline_) {
          deadline_ = per_method_deadline;
          grpc_deadline_state_reset(elem, deadline_);
        }
      }
      // If the service config set wait_for_ready and the application
      // did not explicitly set it, use the value from the service config.
      uint32_t* send_initial_metadata_flags =
          &pending_batches_[0]
               .batch->payload->send_initial_metadata
               .send_initial_metadata_flags;
      if (method_params_->wait_for_ready().has_value() &&
          !(*send_initial_metadata_flags &
            GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET)) {
        if (method_params_->wait_for_ready().value()) {
          *send_initial_metadata_flags |= GRPC_INITIAL_METADATA_WAIT_FOR_READY;
        } else {
          *send_initial_metadata_flags &= ~GRPC_INITIAL_METADATA_WAIT_FOR_READY;
        }
      }
    }
    // Set retry throttle data for call.
    retry_throttle_data_ = chand->retry_throttle_data();
  }
  // If no retry policy, disable retries.
  // TODO(roth): Remove this when adding support for transparent retries.
  if (method_params_ == nullptr || method_params_->retry_policy() == nullptr) {
    enable_retries_ = false;
  }
}

void CallData::MaybeApplyServiceConfigToCallLocked(grpc_call_element* elem) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  // Apply service config data to the call only once, and only if the
  // channel has the data available.
  if (GPR_LIKELY(chand->received_service_config_data() &&
                 !service_config_applied_)) {
    service_config_applied_ = true;
    ApplyServiceConfigToCallLocked(elem);
  }
}

const char* PickResultTypeName(
    LoadBalancingPolicy::PickResult::ResultType type) {
  switch (type) {
    case LoadBalancingPolicy::PickResult::PICK_COMPLETE:
      return "COMPLETE";
    case LoadBalancingPolicy::PickResult::PICK_QUEUE:
      return "QUEUE";
    case LoadBalancingPolicy::PickResult::PICK_FAILED:
      return "FAILED";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

void CallData::PickSubchannel(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  bool pick_complete;
  {
    MutexLock lock(chand->data_plane_mu());
    pick_complete = calld->PickSubchannelLocked(elem, &error);
  }
  if (pick_complete) {
    PickDone(elem, error);
    GRPC_ERROR_UNREF(error);
  }
}

bool CallData::PickSubchannelLocked(grpc_call_element* elem,
                                    grpc_error** error) {
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  GPR_ASSERT(connected_subchannel_ == nullptr);
  GPR_ASSERT(subchannel_call_ == nullptr);
  // The picker being null means that the channel is currently in IDLE state.
  // The incoming call will make the channel exit IDLE.
  if (chand->picker() == nullptr) {
    GRPC_CHANNEL_STACK_REF(chand->owning_stack(), "PickSubchannelLocked");
    // Bounce into the control plane work serializer to exit IDLE. Since we are
    // holding on to the data plane mutex here, we offload it on the ExecCtx so
    // that we don't deadlock with ourselves.
    ExecCtx::Run(
        DEBUG_LOCATION,
        GRPC_CLOSURE_CREATE(
            [](void* arg, grpc_error* /*error*/) {
              auto* chand = static_cast<ChannelData*>(arg);
              chand->work_serializer()->Run(
                  [chand]() {
                    chand->CheckConnectivityState(/*try_to_connect=*/true);
                    GRPC_CHANNEL_STACK_UNREF(chand->owning_stack(),
                                             "PickSubchannelLocked");
                  },
                  DEBUG_LOCATION);
            },
            chand, nullptr),
        GRPC_ERROR_NONE);
    // Queue the pick, so that it will be attempted once the channel
    // becomes connected.
    AddCallToQueuedPicksLocked(elem);
    return false;
  }
  // Apply service config to call if needed.
  MaybeApplyServiceConfigToCallLocked(elem);
  // If this is a retry, use the send_initial_metadata payload that
  // we've cached; otherwise, use the pending batch.  The
  // send_initial_metadata batch will be the first pending batch in the
  // list, as set by GetBatchIndex() above.
  // TODO(roth): What if the LB policy needs to add something to the
  // call's initial metadata, and then there's a retry?  We don't want
  // the new metadata to be added twice.  We might need to somehow
  // allocate the subchannel batch earlier so that we can give the
  // subchannel's copy of the metadata batch (which is copied for each
  // attempt) to the LB policy instead the one from the parent channel.
  LoadBalancingPolicy::PickArgs pick_args;
  pick_args.call_state = &lb_call_state_;
  Metadata initial_metadata(
      this,
      seen_send_initial_metadata_
          ? &send_initial_metadata_
          : pending_batches_[0]
                .batch->payload->send_initial_metadata.send_initial_metadata);
  pick_args.initial_metadata = &initial_metadata;
  // Grab initial metadata flags so that we can check later if the call has
  // wait_for_ready enabled.
  const uint32_t send_initial_metadata_flags =
      seen_send_initial_metadata_ ? send_initial_metadata_flags_
                                  : pending_batches_[0]
                                        .batch->payload->send_initial_metadata
                                        .send_initial_metadata_flags;
  // Attempt pick.
  auto result = chand->picker()->Pick(pick_args);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: LB pick returned %s (subchannel=%p, error=%s)",
            chand, this, PickResultTypeName(result.type),
            result.subchannel.get(), grpc_error_string(result.error));
  }
  switch (result.type) {
    case LoadBalancingPolicy::PickResult::PICK_FAILED: {
      // If we're shutting down, fail all RPCs.
      grpc_error* disconnect_error = chand->disconnect_error();
      if (disconnect_error != GRPC_ERROR_NONE) {
        GRPC_ERROR_UNREF(result.error);
        if (pick_queued_) RemoveCallFromQueuedPicksLocked(elem);
        *error = GRPC_ERROR_REF(disconnect_error);
        return true;
      }
      // If wait_for_ready is false, then the error indicates the RPC
      // attempt's final status.
      if ((send_initial_metadata_flags &
           GRPC_INITIAL_METADATA_WAIT_FOR_READY) == 0) {
        // Retry if appropriate; otherwise, fail.
        grpc_status_code status = GRPC_STATUS_OK;
        grpc_error_get_status(result.error, deadline_, &status, nullptr,
                              nullptr, nullptr);
        const bool retried = enable_retries_ &&
                             MaybeRetry(elem, nullptr /* batch_data */, status,
                                        nullptr /* server_pushback_md */);
        if (!retried) {
          grpc_error* new_error =
              GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                  "Failed to pick subchannel", &result.error, 1);
          GRPC_ERROR_UNREF(result.error);
          *error = new_error;
        }
        if (pick_queued_) RemoveCallFromQueuedPicksLocked(elem);
        return !retried;
      }
      // If wait_for_ready is true, then queue to retry when we get a new
      // picker.
      GRPC_ERROR_UNREF(result.error);
    }
    // Fallthrough
    case LoadBalancingPolicy::PickResult::PICK_QUEUE:
      if (!pick_queued_) AddCallToQueuedPicksLocked(elem);
      return false;
    default:  // PICK_COMPLETE
      if (pick_queued_) RemoveCallFromQueuedPicksLocked(elem);
      // Handle drops.
      if (GPR_UNLIKELY(result.subchannel == nullptr)) {
        result.error = grpc_error_set_int(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Call dropped by load balancing policy"),
            GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
      } else {
        // Grab a ref to the connected subchannel while we're still
        // holding the data plane mutex.
        connected_subchannel_ =
            chand->GetConnectedSubchannelInDataPlane(result.subchannel.get());
        GPR_ASSERT(connected_subchannel_ != nullptr);
      }
      lb_recv_trailing_metadata_ready_ = result.recv_trailing_metadata_ready;
      *error = result.error;
      return true;
  }
}

}  // namespace
}  // namespace grpc_core

/*************************************************************************
 * EXPORTED SYMBOLS
 */

using grpc_core::CallData;
using grpc_core::ChannelData;

const grpc_channel_filter grpc_client_channel_filter = {
    CallData::StartTransportStreamOpBatch,
    ChannelData::StartTransportOp,
    sizeof(CallData),
    CallData::Init,
    CallData::SetPollent,
    CallData::Destroy,
    sizeof(ChannelData),
    ChannelData::Init,
    ChannelData::Destroy,
    ChannelData::GetChannelInfo,
    "client-channel",
};

grpc_connectivity_state grpc_client_channel_check_connectivity_state(
    grpc_channel_element* elem, int try_to_connect) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  return chand->CheckConnectivityState(try_to_connect);
}

int grpc_client_channel_num_external_connectivity_watchers(
    grpc_channel_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  return chand->NumExternalConnectivityWatchers();
}

void grpc_client_channel_watch_connectivity_state(
    grpc_channel_element* elem, grpc_polling_entity pollent,
    grpc_connectivity_state* state, grpc_closure* closure,
    grpc_closure* watcher_timer_init) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  if (state == nullptr) {
    // Handle cancellation.
    GPR_ASSERT(watcher_timer_init == nullptr);
    chand->RemoveExternalConnectivityWatcher(closure, /*cancel=*/true);
    return;
  }
  // Handle addition.
  return chand->AddExternalConnectivityWatcher(pollent, state, closure,
                                               watcher_timer_init);
}

void grpc_client_channel_start_connectivity_watch(
    grpc_channel_element* elem, grpc_connectivity_state initial_state,
    grpc_core::OrphanablePtr<grpc_core::AsyncConnectivityStateWatcherInterface>
        watcher) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->AddConnectivityWatcher(initial_state, std::move(watcher));
}

void grpc_client_channel_stop_connectivity_watch(
    grpc_channel_element* elem,
    grpc_core::AsyncConnectivityStateWatcherInterface* watcher) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->RemoveConnectivityWatcher(watcher);
}

grpc_core::RefCountedPtr<grpc_core::SubchannelCall>
grpc_client_channel_get_subchannel_call(grpc_call_element* elem) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  return calld->subchannel_call();
}
