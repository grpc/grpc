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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/client_channel.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <set>

#include "absl/container/inlined_vector.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/backend_metric.h"
#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/dynamic_filters.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/retry_filter.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"

//
// Client channel filter
//

#define GRPC_ARG_HEALTH_CHECK_SERVICE_NAME \
  "grpc.internal.health_check_service_name"

namespace grpc_core {

using internal::ClientChannelGlobalParsedConfig;
using internal::ClientChannelMethodParsedConfig;
using internal::ClientChannelServiceConfigParser;

TraceFlag grpc_client_channel_call_trace(false, "client_channel_call");
TraceFlag grpc_client_channel_routing_trace(false, "client_channel_routing");

//
// ClientChannel::CallData definition
//

class ClientChannel::CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* final_info,
                      grpc_closure* then_schedule_closure);
  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
  static void SetPollent(grpc_call_element* elem, grpc_polling_entity* pollent);

  // Invoked by channel for queued calls when name resolution is completed.
  static void CheckResolution(void* arg, grpc_error_handle error);
  // Helper function for applying the service config to a call while
  // holding ClientChannel::resolution_mu_.
  // Returns true if the service config has been applied to the call, in which
  // case the caller must invoke ResolutionDone() or AsyncResolutionDone()
  // with the returned error.
  bool CheckResolutionLocked(grpc_call_element* elem, grpc_error_handle* error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);
  // Schedules a callback to continue processing the call once
  // resolution is complete.  The callback will not run until after this
  // method returns.
  void AsyncResolutionDone(grpc_call_element* elem, grpc_error_handle error);

 private:
  class ResolverQueuedCallCanceller;

  CallData(grpc_call_element* elem, const ClientChannel& chand,
           const grpc_call_element_args& args);
  ~CallData();

  // Returns the index into pending_batches_ to be used for batch.
  static size_t GetBatchIndex(grpc_transport_stream_op_batch* batch);
  void PendingBatchesAdd(grpc_call_element* elem,
                         grpc_transport_stream_op_batch* batch);
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
      grpc_call_element* elem, grpc_error_handle error,
      YieldCallCombinerPredicate yield_call_combiner_predicate);
  static void ResumePendingBatchInCallCombiner(void* arg,
                                               grpc_error_handle ignored);
  // Resumes all pending batches on lb_call_.
  void PendingBatchesResume(grpc_call_element* elem);

  // Applies service config to the call.  Must be invoked once we know
  // that the resolver has returned results to the channel.
  // If an error is returned, the error indicates the status with which
  // the call should be failed.
  grpc_error_handle ApplyServiceConfigToCallLocked(
      grpc_call_element* elem, grpc_metadata_batch* initial_metadata)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);
  // Invoked when the resolver result is applied to the caller, on both
  // success or failure.
  static void ResolutionDone(void* arg, grpc_error_handle error);
  // Removes the call (if present) from the channel's list of calls queued
  // for name resolution.
  void MaybeRemoveCallFromResolverQueuedCallsLocked(grpc_call_element* elem)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);
  // Adds the call (if not already present) to the channel's list of
  // calls queued for name resolution.
  void MaybeAddCallToResolverQueuedCallsLocked(grpc_call_element* elem)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);

  static void RecvTrailingMetadataReadyForConfigSelectorCommitCallback(
      void* arg, grpc_error_handle error);
  void InjectRecvTrailingMetadataReadyForConfigSelectorCommitCallback(
      grpc_transport_stream_op_batch* batch);

  void CreateDynamicCall(grpc_call_element* elem);

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

  grpc_polling_entity* pollent_ = nullptr;

  grpc_closure resolution_done_closure_;

  // Accessed while holding ClientChannel::resolution_mu_.
  bool service_config_applied_ ABSL_GUARDED_BY(&ClientChannel::resolution_mu_) =
      false;
  bool queued_pending_resolver_result_
      ABSL_GUARDED_BY(&ClientChannel::resolution_mu_) = false;
  ClientChannel::ResolverQueuedCall resolver_queued_call_
      ABSL_GUARDED_BY(&ClientChannel::resolution_mu_);
  ResolverQueuedCallCanceller* resolver_call_canceller_
      ABSL_GUARDED_BY(&ClientChannel::resolution_mu_) = nullptr;

  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  grpc_closure recv_trailing_metadata_ready_;

  RefCountedPtr<DynamicFilters> dynamic_filters_;
  RefCountedPtr<DynamicFilters::Call> dynamic_call_;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the LB call and are not intercepting any of
  // its callbacks).
  grpc_transport_stream_op_batch* pending_batches_[MAX_PENDING_BATCHES] = {};

  // Set when we get a cancel_stream op.
  grpc_error_handle cancel_error_ = GRPC_ERROR_NONE;
};

//
// Filter vtable
//

const grpc_channel_filter ClientChannel::kFilterVtable = {
    ClientChannel::CallData::StartTransportStreamOpBatch,
    ClientChannel::StartTransportOp,
    sizeof(ClientChannel::CallData),
    ClientChannel::CallData::Init,
    ClientChannel::CallData::SetPollent,
    ClientChannel::CallData::Destroy,
    sizeof(ClientChannel),
    ClientChannel::Init,
    ClientChannel::Destroy,
    ClientChannel::GetChannelInfo,
    "client-channel",
};

//
// dynamic termination filter
//

namespace {

// Channel arg pointer vtable for GRPC_ARG_CLIENT_CHANNEL.
void* ClientChannelArgCopy(void* p) { return p; }
void ClientChannelArgDestroy(void* /*p*/) {}
int ClientChannelArgCmp(void* p, void* q) { return QsortCompare(p, q); }
const grpc_arg_pointer_vtable kClientChannelArgPointerVtable = {
    ClientChannelArgCopy, ClientChannelArgDestroy, ClientChannelArgCmp};

// Channel arg pointer vtable for GRPC_ARG_SERVICE_CONFIG_OBJ.
void* ServiceConfigObjArgCopy(void* p) {
  auto* service_config = static_cast<ServiceConfig*>(p);
  service_config->Ref().release();
  return p;
}
void ServiceConfigObjArgDestroy(void* p) {
  auto* service_config = static_cast<ServiceConfig*>(p);
  service_config->Unref();
}
int ServiceConfigObjArgCmp(void* p, void* q) { return QsortCompare(p, q); }
const grpc_arg_pointer_vtable kServiceConfigObjArgPointerVtable = {
    ServiceConfigObjArgCopy, ServiceConfigObjArgDestroy,
    ServiceConfigObjArgCmp};

class DynamicTerminationFilter {
 public:
  class CallData;

  static const grpc_channel_filter kFilterVtable;

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args) {
    GPR_ASSERT(args->is_last);
    GPR_ASSERT(elem->filter == &kFilterVtable);
    new (elem->channel_data) DynamicTerminationFilter(args->channel_args);
    return GRPC_ERROR_NONE;
  }

  static void Destroy(grpc_channel_element* elem) {
    auto* chand = static_cast<DynamicTerminationFilter*>(elem->channel_data);
    chand->~DynamicTerminationFilter();
  }

  // Will never be called.
  static void StartTransportOp(grpc_channel_element* /*elem*/,
                               grpc_transport_op* /*op*/) {}
  static void GetChannelInfo(grpc_channel_element* /*elem*/,
                             const grpc_channel_info* /*info*/) {}

 private:
  explicit DynamicTerminationFilter(const grpc_channel_args* args)
      : chand_(grpc_channel_args_find_pointer<ClientChannel>(
            args, GRPC_ARG_CLIENT_CHANNEL)) {}

  ClientChannel* chand_;
};

class DynamicTerminationFilter::CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args) {
    new (elem->call_data) CallData(*args);
    return GRPC_ERROR_NONE;
  }

  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /*final_info*/,
                      grpc_closure* then_schedule_closure) {
    auto* calld = static_cast<CallData*>(elem->call_data);
    RefCountedPtr<SubchannelCall> subchannel_call;
    if (GPR_LIKELY(calld->lb_call_ != nullptr)) {
      subchannel_call = calld->lb_call_->subchannel_call();
    }
    calld->~CallData();
    if (GPR_LIKELY(subchannel_call != nullptr)) {
      subchannel_call->SetAfterCallStackDestroy(then_schedule_closure);
    } else {
      // TODO(yashkt) : This can potentially be a Closure::Run
      ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, GRPC_ERROR_NONE);
    }
  }

  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
    auto* calld = static_cast<CallData*>(elem->call_data);
    calld->lb_call_->StartTransportStreamOpBatch(batch);
  }

  static void SetPollent(grpc_call_element* elem,
                         grpc_polling_entity* pollent) {
    auto* calld = static_cast<CallData*>(elem->call_data);
    auto* chand = static_cast<DynamicTerminationFilter*>(elem->channel_data);
    ClientChannel* client_channel = chand->chand_;
    grpc_call_element_args args = {calld->owning_call_,  nullptr,
                                   calld->call_context_, calld->path_,
                                   /*start_time=*/0,     calld->deadline_,
                                   calld->arena_,        calld->call_combiner_};
    auto* service_config_call_data =
        static_cast<ClientChannelServiceConfigCallData*>(
            calld->call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
    calld->lb_call_ = client_channel->CreateLoadBalancedCall(
        args, pollent, nullptr,
        service_config_call_data->call_dispatch_controller(),
        /*is_transparent_retry=*/false);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p dynamic_termination_calld=%p: create lb_call=%p", chand,
              client_channel, calld->lb_call_.get());
    }
  }

 private:
  explicit CallData(const grpc_call_element_args& args)
      : path_(grpc_slice_ref_internal(args.path)),
        deadline_(args.deadline),
        arena_(args.arena),
        owning_call_(args.call_stack),
        call_combiner_(args.call_combiner),
        call_context_(args.context) {}

  ~CallData() { grpc_slice_unref_internal(path_); }

  grpc_slice path_;  // Request path.
  grpc_millis deadline_;
  Arena* arena_;
  grpc_call_stack* owning_call_;
  CallCombiner* call_combiner_;
  grpc_call_context_element* call_context_;

  OrphanablePtr<ClientChannel::LoadBalancedCall> lb_call_;
};

const grpc_channel_filter DynamicTerminationFilter::kFilterVtable = {
    DynamicTerminationFilter::CallData::StartTransportStreamOpBatch,
    DynamicTerminationFilter::StartTransportOp,
    sizeof(DynamicTerminationFilter::CallData),
    DynamicTerminationFilter::CallData::Init,
    DynamicTerminationFilter::CallData::SetPollent,
    DynamicTerminationFilter::CallData::Destroy,
    sizeof(DynamicTerminationFilter),
    DynamicTerminationFilter::Init,
    DynamicTerminationFilter::Destroy,
    DynamicTerminationFilter::GetChannelInfo,
    "dynamic_filter_termination",
};

}  // namespace

//
// ClientChannel::ResolverResultHandler
//

class ClientChannel::ResolverResultHandler : public Resolver::ResultHandler {
 public:
  explicit ResolverResultHandler(ClientChannel* chand) : chand_(chand) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ResolverResultHandler");
  }

  ~ResolverResultHandler() override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO, "chand=%p: resolver shutdown complete", chand_);
    }
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_, "ResolverResultHandler");
  }

  void ReportResult(Resolver::Result result) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    chand_->OnResolverResultChangedLocked(std::move(result));
  }

 private:
  ClientChannel* chand_;
};

//
// ClientChannel::SubchannelWrapper
//

// This class is a wrapper for Subchannel that hides details of the
// channel's implementation (such as the health check service name and
// connected subchannel) from the LB policy API.
//
// Note that no synchronization is needed here, because even if the
// underlying subchannel is shared between channels, this wrapper will only
// be used within one channel, so it will always be synchronized by the
// control plane work_serializer.
class ClientChannel::SubchannelWrapper : public SubchannelInterface {
 public:
  SubchannelWrapper(ClientChannel* chand, RefCountedPtr<Subchannel> subchannel,
                    absl::optional<std::string> health_check_service_name)
      : SubchannelInterface(
            GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)
                ? "SubchannelWrapper"
                : nullptr),
        chand_(chand),
        subchannel_(std::move(subchannel)),
        health_check_service_name_(std::move(health_check_service_name)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: creating subchannel wrapper %p for subchannel %p",
              chand, this, subchannel_.get());
    }
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "SubchannelWrapper");
    if (chand_->channelz_node_ != nullptr) {
      auto* subchannel_node = subchannel_->channelz_node();
      if (subchannel_node != nullptr) {
        auto it = chand_->subchannel_refcount_map_.find(subchannel_.get());
        if (it == chand_->subchannel_refcount_map_.end()) {
          chand_->channelz_node_->AddChildSubchannel(subchannel_node->uuid());
          it = chand_->subchannel_refcount_map_.emplace(subchannel_.get(), 0)
                   .first;
        }
        ++it->second;
      }
    }
    chand_->subchannel_wrappers_.insert(this);
  }

  ~SubchannelWrapper() override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: destroying subchannel wrapper %p for subchannel %p",
              chand_, this, subchannel_.get());
    }
    chand_->subchannel_wrappers_.erase(this);
    if (chand_->channelz_node_ != nullptr) {
      auto* subchannel_node = subchannel_->channelz_node();
      if (subchannel_node != nullptr) {
        auto it = chand_->subchannel_refcount_map_.find(subchannel_.get());
        GPR_ASSERT(it != chand_->subchannel_refcount_map_.end());
        --it->second;
        if (it->second == 0) {
          chand_->channelz_node_->RemoveChildSubchannel(
              subchannel_node->uuid());
          chand_->subchannel_refcount_map_.erase(it);
        }
      }
    }
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_, "SubchannelWrapper");
  }

  grpc_connectivity_state CheckConnectivityState() override {
    return subchannel_->CheckConnectivityState(health_check_service_name_);
  }

  void WatchConnectivityState(
      grpc_connectivity_state initial_state,
      std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    auto& watcher_wrapper = watcher_map_[watcher.get()];
    GPR_ASSERT(watcher_wrapper == nullptr);
    watcher_wrapper = new WatcherWrapper(std::move(watcher),
                                         Ref(DEBUG_LOCATION, "WatcherWrapper"),
                                         initial_state);
    subchannel_->WatchConnectivityState(
        initial_state, health_check_service_name_,
        RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface>(
            watcher_wrapper));
  }

  void CancelConnectivityStateWatch(ConnectivityStateWatcherInterface* watcher)
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    auto it = watcher_map_.find(watcher);
    GPR_ASSERT(it != watcher_map_.end());
    subchannel_->CancelConnectivityStateWatch(health_check_service_name_,
                                              it->second);
    watcher_map_.erase(it);
  }

  RefCountedPtr<ConnectedSubchannel> connected_subchannel() const {
    return subchannel_->connected_subchannel();
  }

  void AttemptToConnect() override { subchannel_->AttemptToConnect(); }

  void ResetBackoff() override { subchannel_->ResetBackoff(); }

  const grpc_channel_args* channel_args() override {
    return subchannel_->channel_args();
  }

  void ThrottleKeepaliveTime(int new_keepalive_time) {
    subchannel_->ThrottleKeepaliveTime(new_keepalive_time);
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

    ~WatcherWrapper() override {
      auto* parent = parent_.release();  // ref owned by lambda
      parent->chand_->work_serializer_->Run(
          [parent]()
              ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_->chand_->work_serializer_) {
                parent->Unref(DEBUG_LOCATION, "WatcherWrapper");
              },
          DEBUG_LOCATION);
    }

    void OnConnectivityStateChange() override {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: connectivity change for subchannel wrapper %p "
                "subchannel %p; hopping into work_serializer",
                parent_->chand_, parent_.get(), parent_->subchannel_.get());
      }
      Ref().release();  // ref owned by lambda
      parent_->chand_->work_serializer_->Run(
          [this]()
              ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_->chand_->work_serializer_) {
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
    void ApplyUpdateInControlPlaneWorkSerializer()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_->chand_->work_serializer_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: processing connectivity change in work serializer "
                "for subchannel wrapper %p subchannel %p "
                "watcher=%p",
                parent_->chand_, parent_.get(), parent_->subchannel_.get(),
                watcher_.get());
      }
      ConnectivityStateChange state_change = PopConnectivityStateChange();
      absl::optional<absl::Cord> keepalive_throttling =
          state_change.status.GetPayload(kKeepaliveThrottlingKey);
      if (keepalive_throttling.has_value()) {
        int new_keepalive_time = -1;
        if (absl::SimpleAtoi(std::string(keepalive_throttling.value()),
                             &new_keepalive_time)) {
          if (new_keepalive_time > parent_->chand_->keepalive_time_) {
            parent_->chand_->keepalive_time_ = new_keepalive_time;
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
              gpr_log(GPR_INFO, "chand=%p: throttling keepalive time to %d",
                      parent_->chand_, parent_->chand_->keepalive_time_);
            }
            // Propagate the new keepalive time to all subchannels. This is so
            // that new transports created by any subchannel (and not just the
            // subchannel that received the GOAWAY), use the new keepalive time.
            for (auto* subchannel_wrapper :
                 parent_->chand_->subchannel_wrappers_) {
              subchannel_wrapper->ThrottleKeepaliveTime(new_keepalive_time);
            }
          }
        } else {
          gpr_log(GPR_ERROR, "chand=%p: Illegal keepalive throttling value %s",
                  parent_->chand_,
                  std::string(keepalive_throttling.value()).c_str());
        }
      }
      // Ignore update if the parent WatcherWrapper has been replaced
      // since this callback was scheduled.
      if (watcher_ != nullptr) {
        last_seen_state_ = state_change.state;
        watcher_->OnConnectivityStateChange(state_change.state);
      }
    }

    std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
        watcher_;
    RefCountedPtr<SubchannelWrapper> parent_;
    grpc_connectivity_state last_seen_state_;
    WatcherWrapper* replacement_ = nullptr;
  };

  ClientChannel* chand_;
  RefCountedPtr<Subchannel> subchannel_;
  absl::optional<std::string> health_check_service_name_;
  // Maps from the address of the watcher passed to us by the LB policy
  // to the address of the WrapperWatcher that we passed to the underlying
  // subchannel.  This is needed so that when the LB policy calls
  // CancelConnectivityStateWatch() with its watcher, we know the
  // corresponding WrapperWatcher to cancel on the underlying subchannel.
  std::map<ConnectivityStateWatcherInterface*, WatcherWrapper*> watcher_map_
      ABSL_GUARDED_BY(&ClientChannel::work_serializer_);
};

//
// ClientChannel::ExternalConnectivityWatcher
//

ClientChannel::ExternalConnectivityWatcher::ExternalConnectivityWatcher(
    ClientChannel* chand, grpc_polling_entity pollent,
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
  {
    MutexLock lock(&chand_->external_watchers_mu_);
    // Will be deleted when the watch is complete.
    GPR_ASSERT(chand->external_watchers_[on_complete] == nullptr);
    // Store a ref to the watcher in the external_watchers_ map.
    chand->external_watchers_[on_complete] =
        Ref(DEBUG_LOCATION, "AddWatcherToExternalWatchersMapLocked");
  }
  // Pass the ref from creating the object to Start().
  chand_->work_serializer_->Run(
      [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
        // The ref is passed to AddWatcherLocked().
        AddWatcherLocked();
      },
      DEBUG_LOCATION);
}

ClientChannel::ExternalConnectivityWatcher::~ExternalConnectivityWatcher() {
  grpc_polling_entity_del_from_pollset_set(&pollent_,
                                           chand_->interested_parties_);
  GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_,
                           "ExternalConnectivityWatcher");
}

void ClientChannel::ExternalConnectivityWatcher::
    RemoveWatcherFromExternalWatchersMap(ClientChannel* chand,
                                         grpc_closure* on_complete,
                                         bool cancel) {
  RefCountedPtr<ExternalConnectivityWatcher> watcher;
  {
    MutexLock lock(&chand->external_watchers_mu_);
    auto it = chand->external_watchers_.find(on_complete);
    if (it != chand->external_watchers_.end()) {
      watcher = std::move(it->second);
      chand->external_watchers_.erase(it);
    }
  }
  // watcher->Cancel() will hop into the WorkSerializer, so we have to unlock
  // the mutex before calling it.
  if (watcher != nullptr && cancel) watcher->Cancel();
}

void ClientChannel::ExternalConnectivityWatcher::Notify(
    grpc_connectivity_state state, const absl::Status& /* status */) {
  bool done = false;
  if (!done_.compare_exchange_strong(done, true, std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
    return;  // Already done.
  }
  // Remove external watcher.
  ExternalConnectivityWatcher::RemoveWatcherFromExternalWatchersMap(
      chand_, on_complete_, /*cancel=*/false);
  // Report new state to the user.
  *state_ = state;
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, GRPC_ERROR_NONE);
  // Hop back into the work_serializer to clean up.
  // Not needed in state SHUTDOWN, because the tracker will
  // automatically remove all watchers in that case.
  if (state != GRPC_CHANNEL_SHUTDOWN) {
    chand_->work_serializer_->Run(
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
          RemoveWatcherLocked();
        },
        DEBUG_LOCATION);
  }
}

void ClientChannel::ExternalConnectivityWatcher::Cancel() {
  bool done = false;
  if (!done_.compare_exchange_strong(done, true, std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
    return;  // Already done.
  }
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, GRPC_ERROR_CANCELLED);
  // Hop back into the work_serializer to clean up.
  chand_->work_serializer_->Run(
      [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
        RemoveWatcherLocked();
      },
      DEBUG_LOCATION);
}

void ClientChannel::ExternalConnectivityWatcher::AddWatcherLocked() {
  Closure::Run(DEBUG_LOCATION, watcher_timer_init_, GRPC_ERROR_NONE);
  // Add new watcher. Pass the ref of the object from creation to OrphanablePtr.
  chand_->state_tracker_.AddWatcher(
      initial_state_, OrphanablePtr<ConnectivityStateWatcherInterface>(this));
}

void ClientChannel::ExternalConnectivityWatcher::RemoveWatcherLocked() {
  chand_->state_tracker_.RemoveWatcher(this);
}

//
// ClientChannel::ConnectivityWatcherAdder
//

class ClientChannel::ConnectivityWatcherAdder {
 public:
  ConnectivityWatcherAdder(
      ClientChannel* chand, grpc_connectivity_state initial_state,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher)
      : chand_(chand),
        initial_state_(initial_state),
        watcher_(std::move(watcher)) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ConnectivityWatcherAdder");
    chand_->work_serializer_->Run(
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
          AddWatcherLocked();
        },
        DEBUG_LOCATION);
  }

 private:
  void AddWatcherLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    chand_->state_tracker_.AddWatcher(initial_state_, std::move(watcher_));
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_, "ConnectivityWatcherAdder");
    delete this;
  }

  ClientChannel* chand_;
  grpc_connectivity_state initial_state_;
  OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher_;
};

//
// ClientChannel::ConnectivityWatcherRemover
//

class ClientChannel::ConnectivityWatcherRemover {
 public:
  ConnectivityWatcherRemover(ClientChannel* chand,
                             AsyncConnectivityStateWatcherInterface* watcher)
      : chand_(chand), watcher_(watcher) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ConnectivityWatcherRemover");
    chand_->work_serializer_->Run(
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
          RemoveWatcherLocked();
        },
        DEBUG_LOCATION);
  }

 private:
  void RemoveWatcherLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    chand_->state_tracker_.RemoveWatcher(watcher_);
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_,
                             "ConnectivityWatcherRemover");
    delete this;
  }

  ClientChannel* chand_;
  AsyncConnectivityStateWatcherInterface* watcher_;
};

//
// ClientChannel::ClientChannelControlHelper
//

class ClientChannel::ClientChannelControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ClientChannelControlHelper(ClientChannel* chand) : chand_(chand) {
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ClientChannelControlHelper");
  }

  ~ClientChannelControlHelper() override {
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_,
                             "ClientChannelControlHelper");
  }

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      ServerAddress address, const grpc_channel_args& args) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    if (chand_->resolver_ == nullptr) return nullptr;  // Shutting down.
    // Determine health check service name.
    absl::optional<std::string> health_check_service_name;
    const char* health_check_service_name_arg = grpc_channel_args_find_string(
        &args, GRPC_ARG_HEALTH_CHECK_SERVICE_NAME);
    if (health_check_service_name_arg != nullptr) {
      bool inhibit_health_checking = grpc_channel_args_find_bool(
          &args, GRPC_ARG_INHIBIT_HEALTH_CHECKING, false);
      if (!inhibit_health_checking) {
        health_check_service_name = health_check_service_name_arg;
      }
    }
    // Construct channel args for subchannel.
    // Remove channel args that should not affect subchannel uniqueness.
    absl::InlinedVector<const char*, 4> args_to_remove = {
        GRPC_ARG_HEALTH_CHECK_SERVICE_NAME,
        GRPC_ARG_INHIBIT_HEALTH_CHECKING,
        GRPC_ARG_CHANNELZ_CHANNEL_NODE,
    };
    // Add channel args needed for the subchannel.
    absl::InlinedVector<grpc_arg, 2> args_to_add = {
        SubchannelPoolInterface::CreateChannelArg(
            chand_->subchannel_pool_.get()),
    };
    // Check if default authority arg is already set.
    const char* default_authority =
        grpc_channel_args_find_string(&args, GRPC_ARG_DEFAULT_AUTHORITY);
    // Add args from subchannel address.
    if (address.args() != nullptr) {
      for (size_t j = 0; j < address.args()->num_args; ++j) {
        grpc_arg& arg = address.args()->args[j];
        if (strcmp(arg.key, GRPC_ARG_DEFAULT_AUTHORITY) == 0) {
          // Don't add default authority arg from subchannel address if
          // it's already set at the channel level -- the value from the
          // application should take precedence over what is set by the
          // resolver.
          if (default_authority != nullptr) continue;
          default_authority = arg.value.string;
        }
        args_to_add.emplace_back(arg);
      }
    }
    // If we haven't already set the default authority arg, add it from
    // the channel.
    if (default_authority == nullptr) {
      // Remove it, just in case it's actually present but is the wrong type.
      args_to_remove.push_back(GRPC_ARG_DEFAULT_AUTHORITY);
      args_to_add.push_back(grpc_channel_arg_string_create(
          const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
          const_cast<char*>(chand_->default_authority_.c_str())));
    }
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        &args, args_to_remove.data(), args_to_remove.size(), args_to_add.data(),
        args_to_add.size());
    // Create subchannel.
    RefCountedPtr<Subchannel> subchannel =
        chand_->client_channel_factory_->CreateSubchannel(address.address(),
                                                          new_args);
    grpc_channel_args_destroy(new_args);
    if (subchannel == nullptr) return nullptr;
    // Make sure the subchannel has updated keepalive time.
    subchannel->ThrottleKeepaliveTime(chand_->keepalive_time_);
    // Create and return wrapper for the subchannel.
    return MakeRefCounted<SubchannelWrapper>(
        chand_, std::move(subchannel), std::move(health_check_service_name));
  }

  void UpdateState(
      grpc_connectivity_state state, const absl::Status& status,
      std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    if (chand_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      const char* extra = chand_->disconnect_error_ == GRPC_ERROR_NONE
                              ? ""
                              : " (ignoring -- channel shutting down)";
      gpr_log(GPR_INFO, "chand=%p: update: state=%s status=(%s) picker=%p%s",
              chand_, ConnectivityStateName(state), status.ToString().c_str(),
              picker.get(), extra);
    }
    // Do update only if not shutting down.
    if (chand_->disconnect_error_ == GRPC_ERROR_NONE) {
      chand_->UpdateStateAndPickerLocked(state, status, "helper",
                                         std::move(picker));
    }
  }

  void RequestReresolution() override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    if (chand_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO, "chand=%p: started name re-resolving", chand_);
    }
    chand_->resolver_->RequestReresolutionLocked();
  }

  absl::string_view GetAuthority() override {
    return chand_->default_authority_;
  }

  void AddTraceEvent(TraceSeverity severity, absl::string_view message) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand_->work_serializer_) {
    if (chand_->resolver_ == nullptr) return;  // Shutting down.
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

  ClientChannel* chand_;
};

//
// ClientChannel implementation
//

ClientChannel* ClientChannel::GetFromChannel(grpc_channel* channel) {
  grpc_channel_element* elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel));
  if (elem->filter != &kFilterVtable) return nullptr;
  return static_cast<ClientChannel*>(elem->channel_data);
}

grpc_error_handle ClientChannel::Init(grpc_channel_element* elem,
                                      grpc_channel_element_args* args) {
  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &kFilterVtable);
  grpc_error_handle error = GRPC_ERROR_NONE;
  new (elem->channel_data) ClientChannel(args, &error);
  return error;
}

void ClientChannel::Destroy(grpc_channel_element* elem) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  chand->~ClientChannel();
}

namespace {

RefCountedPtr<SubchannelPoolInterface> GetSubchannelPool(
    const grpc_channel_args* args) {
  const bool use_local_subchannel_pool = grpc_channel_args_find_bool(
      args, GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, false);
  if (use_local_subchannel_pool) {
    return MakeRefCounted<LocalSubchannelPool>();
  }
  return GlobalSubchannelPool::instance();
}

channelz::ChannelNode* GetChannelzNode(const grpc_channel_args* args) {
  return grpc_channel_args_find_pointer<channelz::ChannelNode>(
      args, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
}

}  // namespace

ClientChannel::ClientChannel(grpc_channel_element_args* args,
                             grpc_error_handle* error)
    : deadline_checking_enabled_(
          grpc_deadline_checking_enabled(args->channel_args)),
      owning_stack_(args->channel_stack),
      client_channel_factory_(
          ClientChannelFactory::GetFromChannelArgs(args->channel_args)),
      channelz_node_(GetChannelzNode(args->channel_args)),
      interested_parties_(grpc_pollset_set_create()),
      work_serializer_(std::make_shared<WorkSerializer>()),
      state_tracker_("client_channel", GRPC_CHANNEL_IDLE),
      subchannel_pool_(GetSubchannelPool(args->channel_args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: creating client_channel for channel stack %p",
            this, owning_stack_);
  }
  // Start backup polling.
  grpc_client_channel_start_backup_polling(interested_parties_);
  // Check client channel factory.
  if (client_channel_factory_ == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Missing client channel factory in args for client channel filter");
    return;
  }
  // Get default service config.  If none is specified via the client API,
  // we use an empty config.
  const char* service_config_json = grpc_channel_args_find_string(
      args->channel_args, GRPC_ARG_SERVICE_CONFIG);
  if (service_config_json == nullptr) service_config_json = "{}";
  *error = GRPC_ERROR_NONE;
  default_service_config_ =
      ServiceConfig::Create(args->channel_args, service_config_json, error);
  if (*error != GRPC_ERROR_NONE) {
    default_service_config_.reset();
    return;
  }
  // Get URI to resolve, using proxy mapper if needed.
  const char* server_uri =
      grpc_channel_args_find_string(args->channel_args, GRPC_ARG_SERVER_URI);
  if (server_uri == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "target URI channel arg missing or wrong type in client channel "
        "filter");
    return;
  }
  uri_to_resolve_ = server_uri;
  char* proxy_name = nullptr;
  grpc_channel_args* new_args = nullptr;
  ProxyMapperRegistry::MapName(server_uri, args->channel_args, &proxy_name,
                               &new_args);
  if (proxy_name != nullptr) {
    uri_to_resolve_ = proxy_name;
    gpr_free(proxy_name);
  }
  // Make sure the URI to resolve is valid, so that we know that
  // resolver creation will succeed later.
  if (!ResolverRegistry::IsValidTarget(uri_to_resolve_)) {
    *error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("the target uri is not valid: ", uri_to_resolve_));
    return;
  }
  // Strip out service config channel arg, so that it doesn't affect
  // subchannel uniqueness when the args flow down to that layer.
  const char* arg_to_remove = GRPC_ARG_SERVICE_CONFIG;
  channel_args_ = grpc_channel_args_copy_and_remove(
      new_args != nullptr ? new_args : args->channel_args, &arg_to_remove, 1);
  grpc_channel_args_destroy(new_args);
  // Set initial keepalive time.
  keepalive_time_ = grpc_channel_args_find_integer(
      channel_args_, GRPC_ARG_KEEPALIVE_TIME_MS,
      {-1 /* default value, unset */, 1, INT_MAX});
  // Set default authority.
  const char* default_authority =
      grpc_channel_args_find_string(channel_args_, GRPC_ARG_DEFAULT_AUTHORITY);
  if (default_authority == nullptr) {
    default_authority_ = ResolverRegistry::GetDefaultAuthority(server_uri);
  } else {
    default_authority_ = default_authority;
  }
  // Success.
  *error = GRPC_ERROR_NONE;
}

ClientChannel::~ClientChannel() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: destroying channel", this);
  }
  DestroyResolverAndLbPolicyLocked();
  grpc_channel_args_destroy(channel_args_);
  // Stop backup polling.
  grpc_client_channel_stop_backup_polling(interested_parties_);
  grpc_pollset_set_destroy(interested_parties_);
  GRPC_ERROR_UNREF(disconnect_error_);
}

OrphanablePtr<ClientChannel::LoadBalancedCall>
ClientChannel::CreateLoadBalancedCall(
    const grpc_call_element_args& args, grpc_polling_entity* pollent,
    grpc_closure* on_call_destruction_complete,
    ConfigSelector::CallDispatchController* call_dispatch_controller,
    bool is_transparent_retry) {
  return OrphanablePtr<LoadBalancedCall>(args.arena->New<LoadBalancedCall>(
      this, args, pollent, on_call_destruction_complete,
      call_dispatch_controller, is_transparent_retry));
}

namespace {

RefCountedPtr<LoadBalancingPolicy::Config> ChooseLbPolicy(
    const Resolver::Result& resolver_result,
    const internal::ClientChannelGlobalParsedConfig* parsed_service_config) {
  // Prefer the LB policy config found in the service config.
  if (parsed_service_config->parsed_lb_config() != nullptr) {
    return parsed_service_config->parsed_lb_config();
  }
  // Try the deprecated LB policy name from the service config.
  // If not, try the setting from channel args.
  const char* policy_name = nullptr;
  if (!parsed_service_config->parsed_deprecated_lb_policy().empty()) {
    policy_name = parsed_service_config->parsed_deprecated_lb_policy().c_str();
  } else {
    policy_name = grpc_channel_args_find_string(resolver_result.args,
                                                GRPC_ARG_LB_POLICY_NAME);
  }
  // Use pick_first if nothing was specified and we didn't select grpclb
  // above.
  if (policy_name == nullptr) policy_name = "pick_first";
  // Now that we have the policy name, construct an empty config for it.
  Json config_json = Json::Array{Json::Object{
      {policy_name, Json::Object{}},
  }};
  grpc_error_handle parse_error = GRPC_ERROR_NONE;
  auto lb_policy_config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
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
  GPR_ASSERT(lb_policy_config != nullptr);
  GPR_ASSERT(parse_error == GRPC_ERROR_NONE);
  return lb_policy_config;
}

}  // namespace

void ClientChannel::OnResolverResultChangedLocked(Resolver::Result result) {
  // Handle race conditions.
  if (resolver_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: got resolver result", this);
  }
  // We only want to trace the address resolution in the follow cases:
  // (a) Address resolution resulted in service config change.
  // (b) Address resolution that causes number of backends to go from
  //     zero to non-zero.
  // (c) Address resolution that causes number of backends to go from
  //     non-zero to zero.
  // (d) Address resolution that causes a new LB policy to be created.
  //
  // We track a list of strings to eventually be concatenated and traced.
  absl::InlinedVector<const char*, 3> trace_strings;
  const bool resolution_contains_addresses =
      result.addresses.ok() && !result.addresses->empty();
  if (!resolution_contains_addresses &&
      previous_resolution_contained_addresses_) {
    trace_strings.push_back("Address list became empty");
  } else if (resolution_contains_addresses &&
             !previous_resolution_contained_addresses_) {
    trace_strings.push_back("Address list became non-empty");
  }
  previous_resolution_contained_addresses_ = resolution_contains_addresses;
  std::string service_config_error_string_storage;
  if (!result.service_config.ok()) {
    service_config_error_string_storage =
        result.service_config.status().ToString();
    trace_strings.push_back(service_config_error_string_storage.c_str());
  }
  // Choose the service config.
  RefCountedPtr<ServiceConfig> service_config;
  RefCountedPtr<ConfigSelector> config_selector;
  if (!result.service_config.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO, "chand=%p: resolver returned service config error: %s",
              this, result.service_config.status().ToString().c_str());
    }
    // If the service config was invalid, then fallback to the
    // previously returned service config.
    if (saved_service_config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: resolver returned invalid service config. "
                "Continuing to use previous service config.",
                this);
      }
      service_config = saved_service_config_;
      config_selector = saved_config_selector_;
    } else {
      // We received a service config error and we don't have a
      // previous service config to fall back to.  Put the channel into
      // TRANSIENT_FAILURE.
      OnResolverErrorLocked(result.service_config.status());
      trace_strings.push_back("no valid service config");
    }
  } else if (*result.service_config == nullptr) {
    // Resolver did not return any service config.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: resolver returned no service config. Using default "
              "service config for channel.",
              this);
    }
    service_config = default_service_config_;
  } else {
    // Use ServiceConfig and ConfigSelector returned by resolver.
    service_config = std::move(*result.service_config);
    config_selector = ConfigSelector::GetFromChannelArgs(*result.args);
  }
  // Note: The only case in which service_config is null here is if the resolver
  // returned a service config error and we don't have a previous service
  // config to fall back to.
  if (service_config != nullptr) {
    // Extract global config for client channel.
    const internal::ClientChannelGlobalParsedConfig* parsed_service_config =
        static_cast<const internal::ClientChannelGlobalParsedConfig*>(
            service_config->GetGlobalParsedConfig(
                internal::ClientChannelServiceConfigParser::ParserIndex()));
    // Choose LB policy config.
    RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config =
        ChooseLbPolicy(result, parsed_service_config);
    // Check if the ServiceConfig has changed.
    const bool service_config_changed =
        saved_service_config_ == nullptr ||
        service_config->json_string() != saved_service_config_->json_string();
    // Check if the ConfigSelector has changed.
    const bool config_selector_changed = !ConfigSelector::Equals(
        saved_config_selector_.get(), config_selector.get());
    // If either has changed, apply the global parameters now.
    if (service_config_changed || config_selector_changed) {
      // Update service config in control plane.
      UpdateServiceConfigInControlPlaneLocked(std::move(service_config),
                                              std::move(config_selector),
                                              lb_policy_config->name());
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO, "chand=%p: service config not changed", this);
    }
    // Create or update LB policy, as needed.
    CreateOrUpdateLbPolicyLocked(
        std::move(lb_policy_config),
        parsed_service_config->health_check_service_name(), std::move(result));
    if (service_config_changed || config_selector_changed) {
      // Start using new service config for calls.
      // This needs to happen after the LB policy has been updated, since
      // the ConfigSelector may need the LB policy to know about new
      // destinations before it can send RPCs to those destinations.
      UpdateServiceConfigInDataPlaneLocked();
      // TODO(ncteisen): might be worth somehow including a snippet of the
      // config in the trace, at the risk of bloating the trace logs.
      trace_strings.push_back("Service config changed");
    }
  }
  // Add channel trace event.
  if (!trace_strings.empty()) {
    std::string message =
        absl::StrCat("Resolution event: ", absl::StrJoin(trace_strings, ", "));
    if (channelz_node_ != nullptr) {
      channelz_node_->AddTraceEvent(channelz::ChannelTrace::Severity::Info,
                                    grpc_slice_from_cpp_string(message));
    }
  }
}

void ClientChannel::OnResolverErrorLocked(absl::Status status) {
  if (resolver_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: resolver transient failure: %s", this,
            status.ToString().c_str());
  }
  // If we already have an LB policy from a previous resolution
  // result, then we continue to let it set the connectivity state.
  // Otherwise, we go into TRANSIENT_FAILURE.
  if (lb_policy_ == nullptr) {
    grpc_error_handle error = absl_status_to_grpc_error(status);
    {
      MutexLock lock(&resolution_mu_);
      // Update resolver transient failure.
      resolver_transient_failure_error_ = status;
      // Process calls that were queued waiting for the resolver result.
      for (ResolverQueuedCall* call = resolver_queued_calls_; call != nullptr;
           call = call->next) {
        grpc_call_element* elem = call->elem;
        CallData* calld = static_cast<CallData*>(elem->call_data);
        grpc_error_handle error = GRPC_ERROR_NONE;
        if (calld->CheckResolutionLocked(elem, &error)) {
          calld->AsyncResolutionDone(elem, error);
        }
      }
    }
    GRPC_ERROR_UNREF(error);
    // Update connectivity state.
    UpdateStateAndPickerLocked(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status, "resolver failure",
        absl::make_unique<LoadBalancingPolicy::TransientFailurePicker>(status));
  }
}

void ClientChannel::CreateOrUpdateLbPolicyLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config,
    const absl::optional<std::string>& health_check_service_name,
    Resolver::Result result) {
  // Construct update.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses = std::move(result.addresses);
  update_args.config = std::move(lb_policy_config);
  update_args.resolution_note = std::move(result.resolution_note);
  // Add health check service name to channel args.
  absl::InlinedVector<grpc_arg, 1> args_to_add;
  if (health_check_service_name.has_value()) {
    args_to_add.push_back(grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME),
        const_cast<char*>(health_check_service_name->c_str())));
  }
  // Remove the config selector from channel args so that we're not holding
  // unnecessary refs that cause it to be destroyed somewhere other than in the
  // WorkSerializer.
  const char* arg_to_remove = GRPC_ARG_CONFIG_SELECTOR;
  update_args.args = grpc_channel_args_copy_and_add_and_remove(
      result.args, &arg_to_remove, 1, args_to_add.data(), args_to_add.size());
  // Create policy if needed.
  if (lb_policy_ == nullptr) {
    lb_policy_ = CreateLbPolicyLocked(*update_args.args);
  }
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: Updating child policy %p", this,
            lb_policy_.get());
  }
  lb_policy_->UpdateLocked(std::move(update_args));
}

// Creates a new LB policy.
OrphanablePtr<LoadBalancingPolicy> ClientChannel::CreateLbPolicyLocked(
    const grpc_channel_args& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer_;
  lb_policy_args.channel_control_helper =
      absl::make_unique<ClientChannelControlHelper>(this);
  lb_policy_args.args = &args;
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_client_channel_routing_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: created new LB policy %p", this,
            lb_policy.get());
  }
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties_);
  return lb_policy;
}

void ClientChannel::AddResolverQueuedCall(ResolverQueuedCall* call,
                                          grpc_polling_entity* pollent) {
  // Add call to queued calls list.
  call->next = resolver_queued_calls_;
  resolver_queued_calls_ = call;
  // Add call's pollent to channel's interested_parties, so that I/O
  // can be done under the call's CQ.
  grpc_polling_entity_add_to_pollset_set(pollent, interested_parties_);
}

void ClientChannel::RemoveResolverQueuedCall(ResolverQueuedCall* to_remove,
                                             grpc_polling_entity* pollent) {
  // Remove call's pollent from channel's interested_parties.
  grpc_polling_entity_del_from_pollset_set(pollent, interested_parties_);
  // Remove from queued calls list.
  for (ResolverQueuedCall** call = &resolver_queued_calls_; *call != nullptr;
       call = &(*call)->next) {
    if (*call == to_remove) {
      *call = to_remove->next;
      return;
    }
  }
}

void ClientChannel::UpdateServiceConfigInControlPlaneLocked(
    RefCountedPtr<ServiceConfig> service_config,
    RefCountedPtr<ConfigSelector> config_selector, const char* lb_policy_name) {
  UniquePtr<char> service_config_json(
      gpr_strdup(service_config->json_string().c_str()));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p: resolver returned updated service config: \"%s\"", this,
            service_config_json.get());
  }
  // Save service config.
  saved_service_config_ = std::move(service_config);
  // Swap out the data used by GetChannelInfo().
  UniquePtr<char> lb_policy_name_owned(gpr_strdup(lb_policy_name));
  {
    MutexLock lock(&info_mu_);
    info_lb_policy_name_ = std::move(lb_policy_name_owned);
    info_service_config_json_ = std::move(service_config_json);
  }
  // Save config selector.
  saved_config_selector_ = std::move(config_selector);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: using ConfigSelector %p", this,
            saved_config_selector_.get());
  }
}

void ClientChannel::UpdateServiceConfigInDataPlaneLocked() {
  // Grab ref to service config.
  RefCountedPtr<ServiceConfig> service_config = saved_service_config_;
  // Grab ref to config selector.  Use default if resolver didn't supply one.
  RefCountedPtr<ConfigSelector> config_selector = saved_config_selector_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: switching to ConfigSelector %p", this,
            saved_config_selector_.get());
  }
  if (config_selector == nullptr) {
    config_selector =
        MakeRefCounted<DefaultConfigSelector>(saved_service_config_);
  }
  absl::InlinedVector<grpc_arg, 2> args_to_add = {
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_CLIENT_CHANNEL), this,
          &kClientChannelArgPointerVtable),
      grpc_channel_arg_pointer_create(
          const_cast<char*>(GRPC_ARG_SERVICE_CONFIG_OBJ), service_config.get(),
          &kServiceConfigObjArgPointerVtable),
  };
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
      channel_args_, args_to_add.data(), args_to_add.size());
  new_args = config_selector->ModifyChannelArgs(new_args);
  bool enable_retries =
      grpc_channel_args_find_bool(new_args, GRPC_ARG_ENABLE_RETRIES, true);
  // Construct dynamic filter stack.
  std::vector<const grpc_channel_filter*> filters =
      config_selector->GetFilters();
  if (enable_retries) {
    filters.push_back(&kRetryFilterVtable);
  } else {
    filters.push_back(&DynamicTerminationFilter::kFilterVtable);
  }
  RefCountedPtr<DynamicFilters> dynamic_filters =
      DynamicFilters::Create(new_args, std::move(filters));
  GPR_ASSERT(dynamic_filters != nullptr);
  grpc_channel_args_destroy(new_args);
  // Grab data plane lock to update service config.
  //
  // We defer unreffing the old values (and deallocating memory) until
  // after releasing the lock to keep the critical section small.
  {
    MutexLock lock(&resolution_mu_);
    resolver_transient_failure_error_ = absl::OkStatus();
    // Update service config.
    received_service_config_data_ = true;
    // Old values will be unreffed after lock is released.
    service_config_.swap(service_config);
    config_selector_.swap(config_selector);
    dynamic_filters_.swap(dynamic_filters);
    // Process calls that were queued waiting for the resolver result.
    for (ResolverQueuedCall* call = resolver_queued_calls_; call != nullptr;
         call = call->next) {
      // If there are a lot of queued calls here, resuming them all may cause us
      // to stay inside C-core for a long period of time. All of that work would
      // be done using the same ExecCtx instance and therefore the same cached
      // value of "now". The longer it takes to finish all of this work and exit
      // from C-core, the more stale the cached value of "now" may become. This
      // can cause problems whereby (e.g.) we calculate a timer deadline based
      // on the stale value, which results in the timer firing too early. To
      // avoid this, we invalidate the cached value for each call we process.
      ExecCtx::Get()->InvalidateNow();
      grpc_call_element* elem = call->elem;
      CallData* calld = static_cast<CallData*>(elem->call_data);
      grpc_error_handle error = GRPC_ERROR_NONE;
      if (calld->CheckResolutionLocked(elem, &error)) {
        calld->AsyncResolutionDone(elem, error);
      }
    }
  }
  // Old values will be unreffed after lock is released when they go out
  // of scope.
}

void ClientChannel::CreateResolverLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: starting name resolution", this);
  }
  resolver_ = ResolverRegistry::CreateResolver(
      uri_to_resolve_.c_str(), channel_args_, interested_parties_,
      work_serializer_, absl::make_unique<ResolverResultHandler>(this));
  // Since the validity of the args was checked when the channel was created,
  // CreateResolver() must return a non-null result.
  GPR_ASSERT(resolver_ != nullptr);
  UpdateStateAndPickerLocked(
      GRPC_CHANNEL_CONNECTING, absl::Status(), "started resolving",
      absl::make_unique<LoadBalancingPolicy::QueuePicker>(nullptr));
  resolver_->StartLocked();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p: created resolver=%p", this, resolver_.get());
  }
}

void ClientChannel::DestroyResolverAndLbPolicyLocked() {
  if (resolver_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO, "chand=%p: shutting down resolver=%p", this,
              resolver_.get());
    }
    resolver_.reset();
    if (lb_policy_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO, "chand=%p: shutting down lb_policy=%p", this,
                lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties_);
      lb_policy_.reset();
    }
  }
}

void ClientChannel::UpdateStateAndPickerLocked(
    grpc_connectivity_state state, const absl::Status& status,
    const char* reason,
    std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker) {
  // Special case for IDLE and SHUTDOWN states.
  if (picker == nullptr || state == GRPC_CHANNEL_SHUTDOWN) {
    saved_service_config_.reset();
    saved_config_selector_.reset();
    // Acquire resolution lock to update config selector and associated state.
    // To minimize lock contention, we wait to unref these objects until
    // after we release the lock.
    RefCountedPtr<ServiceConfig> service_config_to_unref;
    RefCountedPtr<ConfigSelector> config_selector_to_unref;
    RefCountedPtr<DynamicFilters> dynamic_filters_to_unref;
    {
      MutexLock lock(&resolution_mu_);
      received_service_config_data_ = false;
      service_config_to_unref = std::move(service_config_);
      config_selector_to_unref = std::move(config_selector_);
      dynamic_filters_to_unref = std::move(dynamic_filters_);
    }
  }
  // Update connectivity state.
  state_tracker_.SetState(state, status, reason);
  if (channelz_node_ != nullptr) {
    channelz_node_->SetConnectivityState(state);
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            channelz::ChannelNode::GetChannelConnectivityStateChangeString(
                state)));
  }
  // Grab data plane lock to update the picker.
  {
    MutexLock lock(&data_plane_mu_);
    // Swap out the picker.
    // Note: Original value will be destroyed after the lock is released.
    picker_.swap(picker);
    // Re-process queued picks.
    for (LbQueuedCall* call = lb_queued_calls_; call != nullptr;
         call = call->next) {
      // If there are a lot of queued calls here, resuming them all may cause us
      // to stay inside C-core for a long period of time. All of that work would
      // be done using the same ExecCtx instance and therefore the same cached
      // value of "now". The longer it takes to finish all of this work and exit
      // from C-core, the more stale the cached value of "now" may become. This
      // can cause problems whereby (e.g.) we calculate a timer deadline based
      // on the stale value, which results in the timer firing too early. To
      // avoid this, we invalidate the cached value for each call we process.
      ExecCtx::Get()->InvalidateNow();
      grpc_error_handle error = GRPC_ERROR_NONE;
      if (call->lb_call->PickSubchannelLocked(&error)) {
        call->lb_call->AsyncPickDone(error);
      }
    }
  }
}

namespace {

// TODO(roth): Remove this in favor of the gprpp Match() function once
// we can do that without breaking lock annotations.
template <typename T>
T HandlePickResult(
    LoadBalancingPolicy::PickResult* result,
    std::function<T(LoadBalancingPolicy::PickResult::Complete*)> complete_func,
    std::function<T(LoadBalancingPolicy::PickResult::Queue*)> queue_func,
    std::function<T(LoadBalancingPolicy::PickResult::Fail*)> fail_func,
    std::function<T(LoadBalancingPolicy::PickResult::Drop*)> drop_func) {
  auto* complete_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Complete>(&result->result);
  if (complete_pick != nullptr) {
    return complete_func(complete_pick);
  }
  auto* queue_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Queue>(&result->result);
  if (queue_pick != nullptr) {
    return queue_func(queue_pick);
  }
  auto* fail_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Fail>(&result->result);
  if (fail_pick != nullptr) {
    return fail_func(fail_pick);
  }
  auto* drop_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Drop>(&result->result);
  GPR_ASSERT(drop_pick != nullptr);
  return drop_func(drop_pick);
}

}  // namespace

grpc_error_handle ClientChannel::DoPingLocked(grpc_transport_op* op) {
  if (state_tracker_.state() != GRPC_CHANNEL_READY) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("channel not connected");
  }
  LoadBalancingPolicy::PickResult result;
  {
    MutexLock lock(&data_plane_mu_);
    result = picker_->Pick(LoadBalancingPolicy::PickArgs());
  }
  return HandlePickResult<grpc_error_handle>(
      &result,
      // Complete pick.
      [op](LoadBalancingPolicy::PickResult::Complete* complete_pick)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::work_serializer_) {
            SubchannelWrapper* subchannel = static_cast<SubchannelWrapper*>(
                complete_pick->subchannel.get());
            RefCountedPtr<ConnectedSubchannel> connected_subchannel =
                subchannel->connected_subchannel();
            connected_subchannel->Ping(op->send_ping.on_initiate,
                                       op->send_ping.on_ack);
            return GRPC_ERROR_NONE;
          },
      // Queue pick.
      [](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/) {
        return GRPC_ERROR_CREATE_FROM_STATIC_STRING("LB picker queued call");
      },
      // Fail pick.
      [](LoadBalancingPolicy::PickResult::Fail* fail_pick) {
        return absl_status_to_grpc_error(fail_pick->status);
      },
      // Drop pick.
      [](LoadBalancingPolicy::PickResult::Drop* drop_pick) {
        return absl_status_to_grpc_error(drop_pick->status);
      });
}

void ClientChannel::StartTransportOpLocked(grpc_transport_op* op) {
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
    grpc_error_handle error = DoPingLocked(op);
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
    if (lb_policy_ != nullptr) {
      lb_policy_->ResetBackoffLocked();
    }
  }
  // Disconnect or enter IDLE.
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p: disconnect_with_error: %s", this,
              grpc_error_std_string(op->disconnect_with_error).c_str());
    }
    DestroyResolverAndLbPolicyLocked();
    intptr_t value;
    if (grpc_error_get_int(op->disconnect_with_error,
                           GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE, &value) &&
        static_cast<grpc_connectivity_state>(value) == GRPC_CHANNEL_IDLE) {
      if (disconnect_error_ == GRPC_ERROR_NONE) {
        // Enter IDLE state.
        UpdateStateAndPickerLocked(GRPC_CHANNEL_IDLE, absl::Status(),
                                   "channel entering IDLE", nullptr);
      }
      GRPC_ERROR_UNREF(op->disconnect_with_error);
    } else {
      // Disconnect.
      GPR_ASSERT(disconnect_error_ == GRPC_ERROR_NONE);
      disconnect_error_ = op->disconnect_with_error;
      UpdateStateAndPickerLocked(
          GRPC_CHANNEL_SHUTDOWN, absl::Status(), "shutdown from API",
          absl::make_unique<LoadBalancingPolicy::TransientFailurePicker>(
              grpc_error_to_absl_status(op->disconnect_with_error)));
    }
  }
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "start_transport_op");
  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
}

void ClientChannel::StartTransportOp(grpc_channel_element* elem,
                                     grpc_transport_op* op) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  GPR_ASSERT(op->set_accept_stream == false);
  // Handle bind_pollset.
  if (op->bind_pollset != nullptr) {
    grpc_pollset_set_add_pollset(chand->interested_parties_, op->bind_pollset);
  }
  // Pop into control plane work_serializer for remaining ops.
  GRPC_CHANNEL_STACK_REF(chand->owning_stack_, "start_transport_op");
  chand->work_serializer_->Run(
      [chand, op]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand->work_serializer_) {
        chand->StartTransportOpLocked(op);
      },
      DEBUG_LOCATION);
}

void ClientChannel::GetChannelInfo(grpc_channel_element* elem,
                                   const grpc_channel_info* info) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  MutexLock lock(&chand->info_mu_);
  if (info->lb_policy_name != nullptr) {
    *info->lb_policy_name = gpr_strdup(chand->info_lb_policy_name_.get());
  }
  if (info->service_config_json != nullptr) {
    *info->service_config_json =
        gpr_strdup(chand->info_service_config_json_.get());
  }
}

void ClientChannel::AddLbQueuedCall(LbQueuedCall* call,
                                    grpc_polling_entity* pollent) {
  // Add call to queued picks list.
  call->next = lb_queued_calls_;
  lb_queued_calls_ = call;
  // Add call's pollent to channel's interested_parties, so that I/O
  // can be done under the call's CQ.
  grpc_polling_entity_add_to_pollset_set(pollent, interested_parties_);
}

void ClientChannel::RemoveLbQueuedCall(LbQueuedCall* to_remove,
                                       grpc_polling_entity* pollent) {
  // Remove call's pollent from channel's interested_parties.
  grpc_polling_entity_del_from_pollset_set(pollent, interested_parties_);
  // Remove from queued picks list.
  for (LbQueuedCall** call = &lb_queued_calls_; *call != nullptr;
       call = &(*call)->next) {
    if (*call == to_remove) {
      *call = to_remove->next;
      return;
    }
  }
}

void ClientChannel::TryToConnectLocked() {
  if (lb_policy_ != nullptr) {
    lb_policy_->ExitIdleLocked();
  } else if (resolver_ == nullptr) {
    CreateResolverLocked();
  }
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "TryToConnect");
}

grpc_connectivity_state ClientChannel::CheckConnectivityState(
    bool try_to_connect) {
  // state_tracker_ is guarded by work_serializer_, which we're not
  // holding here.  But the one method of state_tracker_ that *is*
  // thread-safe to call without external synchronization is the state()
  // method, so we can disable thread-safety analysis for this one read.
  grpc_connectivity_state out = ABSL_TS_UNCHECKED_READ(state_tracker_).state();
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    GRPC_CHANNEL_STACK_REF(owning_stack_, "TryToConnect");
    work_serializer_->Run([this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                              work_serializer_) { TryToConnectLocked(); },
                          DEBUG_LOCATION);
  }
  return out;
}

void ClientChannel::AddConnectivityWatcher(
    grpc_connectivity_state initial_state,
    OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher) {
  new ConnectivityWatcherAdder(this, initial_state, std::move(watcher));
}

void ClientChannel::RemoveConnectivityWatcher(
    AsyncConnectivityStateWatcherInterface* watcher) {
  new ConnectivityWatcherRemover(this, watcher);
}

//
// CallData implementation
//

ClientChannel::CallData::CallData(grpc_call_element* elem,
                                  const ClientChannel& chand,
                                  const grpc_call_element_args& args)
    : deadline_state_(elem, args,
                      GPR_LIKELY(chand.deadline_checking_enabled_)
                          ? args.deadline
                          : GRPC_MILLIS_INF_FUTURE),
      path_(grpc_slice_ref_internal(args.path)),
      call_start_time_(args.start_time),
      deadline_(args.deadline),
      arena_(args.arena),
      owning_call_(args.call_stack),
      call_combiner_(args.call_combiner),
      call_context_(args.context) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: created call", &chand, this);
  }
}

ClientChannel::CallData::~CallData() {
  grpc_slice_unref_internal(path_);
  GRPC_ERROR_UNREF(cancel_error_);
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i] == nullptr);
  }
}

grpc_error_handle ClientChannel::CallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  new (elem->call_data) CallData(elem, *chand, *args);
  return GRPC_ERROR_NONE;
}

void ClientChannel::CallData::Destroy(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* then_schedule_closure) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  RefCountedPtr<DynamicFilters::Call> dynamic_call =
      std::move(calld->dynamic_call_);
  calld->~CallData();
  if (GPR_LIKELY(dynamic_call != nullptr)) {
    dynamic_call->SetAfterCallStackDestroy(then_schedule_closure);
  } else {
    // TODO(yashkt) : This can potentially be a Closure::Run
    ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, GRPC_ERROR_NONE);
  }
}

void ClientChannel::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("cc_start_transport_stream_op_batch", 0);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  if (GPR_LIKELY(chand->deadline_checking_enabled_)) {
    grpc_deadline_state_client_start_transport_stream_op_batch(elem, batch);
  }
  // Intercept recv_trailing_metadata to call CallDispatchController::Commit(),
  // in case we wind up failing the call before we get down to the retry
  // or LB call layer.
  if (batch->recv_trailing_metadata) {
    calld->InjectRecvTrailingMetadataReadyForConfigSelectorCommitCallback(
        batch);
  }
  // If we already have a dynamic call, pass the batch down to it.
  // Note that once we have done so, we do not need to acquire the channel's
  // resolution mutex, which is more efficient (especially for streaming calls).
  if (calld->dynamic_call_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: starting batch on dynamic_call=%p",
              chand, calld, calld->dynamic_call_.get());
    }
    calld->dynamic_call_->StartTransportStreamOpBatch(batch);
    return;
  }
  // We do not yet have a dynamic call.
  //
  // If we've previously been cancelled, immediately fail any new batches.
  if (GPR_UNLIKELY(calld->cancel_error_ != GRPC_ERROR_NONE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: failing batch with error: %s",
              chand, calld,
              grpc_error_std_string(calld->cancel_error_).c_str());
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
              calld, grpc_error_std_string(calld->cancel_error_).c_str());
    }
    // Fail all pending batches.
    calld->PendingBatchesFail(elem, GRPC_ERROR_REF(calld->cancel_error_),
                              NoYieldCallCombiner);
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(calld->cancel_error_), calld->call_combiner_);
    return;
  }
  // Add the batch to the pending list.
  calld->PendingBatchesAdd(elem, batch);
  // For batches containing a send_initial_metadata op, acquire the
  // channel's resolution mutex to apply the service config to the call,
  // after which we will create a dynamic call.
  if (GPR_LIKELY(batch->send_initial_metadata)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: grabbing resolution mutex to apply service "
              "config",
              chand, calld);
    }
    CheckResolution(elem, GRPC_ERROR_NONE);
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

void ClientChannel::CallData::SetPollent(grpc_call_element* elem,
                                         grpc_polling_entity* pollent) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  calld->pollent_ = pollent;
}

//
// pending_batches management
//

size_t ClientChannel::CallData::GetBatchIndex(
    grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in ApplyServiceConfigToCallLocked() and
  // CheckResolutionLocked() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::CallData::PendingBatchesAdd(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: adding pending batch at index %" PRIuPTR, chand,
            this, idx);
  }
  grpc_transport_stream_op_batch*& pending = pending_batches_[idx];
  GPR_ASSERT(pending == nullptr);
  pending = batch;
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::CallData::FailPendingBatchInCallCombiner(
    void* arg, grpc_error_handle error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  CallData* calld = static_cast<CallData*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(
      batch, GRPC_ERROR_REF(error), calld->call_combiner_);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::CallData::PendingBatchesFail(
    grpc_call_element* elem, grpc_error_handle error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            elem->channel_data, this, num_batches,
            grpc_error_std_string(error).c_str());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        FailPendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_REF(error),
                   "PendingBatchesFail");
      batch = nullptr;
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
void ClientChannel::CallData::ResumePendingBatchInCallCombiner(
    void* arg, grpc_error_handle /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* elem =
      static_cast<grpc_call_element*>(batch->handler_private.extra_arg);
  auto* calld = static_cast<CallData*>(elem->call_data);
  // Note: This will release the call combiner.
  calld->dynamic_call_->StartTransportStreamOpBatch(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::CallData::PendingBatchesResume(grpc_call_element* elem) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  // Retries not enabled; send down batches as-is.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " pending batches on dynamic_call=%p",
            chand, this, num_batches, dynamic_call_.get());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = elem;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        ResumePendingBatchInCallCombiner, batch, nullptr);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                   "resuming pending batch from client channel call");
      batch = nullptr;
    }
  }
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner_);
}

//
// name resolution
//

// A class to handle the call combiner cancellation callback for a
// queued pick.
class ClientChannel::CallData::ResolverQueuedCallCanceller {
 public:
  explicit ResolverQueuedCallCanceller(grpc_call_element* elem) : elem_(elem) {
    auto* calld = static_cast<CallData*>(elem->call_data);
    GRPC_CALL_STACK_REF(calld->owning_call_, "ResolverQueuedCallCanceller");
    GRPC_CLOSURE_INIT(&closure_, &CancelLocked, this,
                      grpc_schedule_on_exec_ctx);
    calld->call_combiner_->SetNotifyOnCancel(&closure_);
  }

 private:
  static void CancelLocked(void* arg, grpc_error_handle error) {
    auto* self = static_cast<ResolverQueuedCallCanceller*>(arg);
    auto* chand = static_cast<ClientChannel*>(self->elem_->channel_data);
    auto* calld = static_cast<CallData*>(self->elem_->call_data);
    {
      MutexLock lock(&chand->resolution_mu_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: cancelling resolver queued pick: "
                "error=%s self=%p calld->resolver_pick_canceller=%p",
                chand, calld, grpc_error_std_string(error).c_str(), self,
                calld->resolver_call_canceller_);
      }
      if (calld->resolver_call_canceller_ == self && error != GRPC_ERROR_NONE) {
        // Remove pick from list of queued picks.
        calld->MaybeRemoveCallFromResolverQueuedCallsLocked(self->elem_);
        // Fail pending batches on the call.
        calld->PendingBatchesFail(self->elem_, GRPC_ERROR_REF(error),
                                  YieldCallCombinerIfPendingBatchesFound);
      }
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call_, "ResolvingQueuedCallCanceller");
    delete self;
  }

  grpc_call_element* elem_;
  grpc_closure closure_;
};

void ClientChannel::CallData::MaybeRemoveCallFromResolverQueuedCallsLocked(
    grpc_call_element* elem) {
  if (!queued_pending_resolver_result_) return;
  auto* chand = static_cast<ClientChannel*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: removing from resolver queued picks list",
            chand, this);
  }
  chand->RemoveResolverQueuedCall(&resolver_queued_call_, pollent_);
  queued_pending_resolver_result_ = false;
  // Lame the call combiner canceller.
  resolver_call_canceller_ = nullptr;
}

void ClientChannel::CallData::MaybeAddCallToResolverQueuedCallsLocked(
    grpc_call_element* elem) {
  if (queued_pending_resolver_result_) return;
  auto* chand = static_cast<ClientChannel*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: adding to resolver queued picks list",
            chand, this);
  }
  queued_pending_resolver_result_ = true;
  resolver_queued_call_.elem = elem;
  chand->AddResolverQueuedCall(&resolver_queued_call_, pollent_);
  // Register call combiner cancellation callback.
  resolver_call_canceller_ = new ResolverQueuedCallCanceller(elem);
}

grpc_error_handle ClientChannel::CallData::ApplyServiceConfigToCallLocked(
    grpc_call_element* elem, grpc_metadata_batch* initial_metadata) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: applying service config to call",
            chand, this);
  }
  ConfigSelector* config_selector = chand->config_selector_.get();
  if (config_selector != nullptr) {
    // Use the ConfigSelector to determine the config for the call.
    ConfigSelector::CallConfig call_config =
        config_selector->GetCallConfig({&path_, initial_metadata, arena_});
    if (call_config.error != GRPC_ERROR_NONE) return call_config.error;
    // Create a ClientChannelServiceConfigCallData for the call.  This stores
    // a ref to the ServiceConfig and caches the right set of parsed configs
    // to use for the call.  The ClientChannelServiceConfigCallData will store
    // itself in the call context, so that it can be accessed by filters
    // below us in the stack, and it will be cleaned up when the call ends.
    auto* service_config_call_data =
        arena_->New<ClientChannelServiceConfigCallData>(
            std::move(call_config.service_config), call_config.method_configs,
            std::move(call_config.call_attributes),
            call_config.call_dispatch_controller, call_context_);
    // Apply our own method params to the call.
    auto* method_params = static_cast<ClientChannelMethodParsedConfig*>(
        service_config_call_data->GetMethodParsedConfig(
            internal::ClientChannelServiceConfigParser::ParserIndex()));
    if (method_params != nullptr) {
      // If the deadline from the service config is shorter than the one
      // from the client API, reset the deadline timer.
      if (chand->deadline_checking_enabled_ && method_params->timeout() != 0) {
        const grpc_millis per_method_deadline =
            grpc_cycle_counter_to_millis_round_up(call_start_time_) +
            method_params->timeout();
        if (per_method_deadline < deadline_) {
          deadline_ = per_method_deadline;
          grpc_deadline_state_reset(elem, deadline_);
        }
      }
      // If the service config set wait_for_ready and the application
      // did not explicitly set it, use the value from the service config.
      uint32_t* send_initial_metadata_flags =
          &pending_batches_[0]
               ->payload->send_initial_metadata.send_initial_metadata_flags;
      if (method_params->wait_for_ready().has_value() &&
          !(*send_initial_metadata_flags &
            GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET)) {
        if (method_params->wait_for_ready().value()) {
          *send_initial_metadata_flags |= GRPC_INITIAL_METADATA_WAIT_FOR_READY;
        } else {
          *send_initial_metadata_flags &= ~GRPC_INITIAL_METADATA_WAIT_FOR_READY;
        }
      }
    }
    // Set the dynamic filter stack.
    dynamic_filters_ = chand->dynamic_filters_;
  }
  return GRPC_ERROR_NONE;
}

void ClientChannel::CallData::
    RecvTrailingMetadataReadyForConfigSelectorCommitCallback(
        void* arg, grpc_error_handle error) {
  auto* self = static_cast<CallData*>(arg);
  auto* service_config_call_data =
      static_cast<ClientChannelServiceConfigCallData*>(
          self->call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  if (service_config_call_data != nullptr) {
    service_config_call_data->call_dispatch_controller()->Commit();
  }
  // Chain to original callback.
  Closure::Run(DEBUG_LOCATION, self->original_recv_trailing_metadata_ready_,
               GRPC_ERROR_REF(error));
}

void ClientChannel::CallData::
    InjectRecvTrailingMetadataReadyForConfigSelectorCommitCallback(
        grpc_transport_stream_op_batch* batch) {
  original_recv_trailing_metadata_ready_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReadyForConfigSelectorCommitCallback,
                    this, nullptr);
  batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &recv_trailing_metadata_ready_;
}

void ClientChannel::CallData::AsyncResolutionDone(grpc_call_element* elem,
                                                  grpc_error_handle error) {
  // TODO(roth): Does this callback need to hold a ref to the call stack?
  GRPC_CLOSURE_INIT(&resolution_done_closure_, ResolutionDone, elem, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &resolution_done_closure_, error);
}

void ClientChannel::CallData::ResolutionDone(void* arg,
                                             grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (error != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: error applying config to call: error=%s",
              chand, calld, grpc_error_std_string(error).c_str());
    }
    calld->PendingBatchesFail(elem, GRPC_ERROR_REF(error), YieldCallCombiner);
    return;
  }
  calld->CreateDynamicCall(elem);
}

void ClientChannel::CallData::CheckResolution(void* arg,
                                              grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  bool resolution_complete;
  {
    MutexLock lock(&chand->resolution_mu_);
    resolution_complete = calld->CheckResolutionLocked(elem, &error);
  }
  if (resolution_complete) {
    ResolutionDone(elem, error);
    GRPC_ERROR_UNREF(error);
  }
}

bool ClientChannel::CallData::CheckResolutionLocked(grpc_call_element* elem,
                                                    grpc_error_handle* error) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  // If we're still in IDLE, we need to start resolving.
  if (GPR_UNLIKELY(chand->CheckConnectivityState(false) == GRPC_CHANNEL_IDLE)) {
    // Bounce into the control plane work serializer to start resolving,
    // in case we are still in IDLE state.  Since we are holding on to the
    // resolution mutex here, we offload it on the ExecCtx so that we don't
    // deadlock with ourselves.
    GRPC_CHANNEL_STACK_REF(chand->owning_stack_, "CheckResolutionLocked");
    ExecCtx::Run(
        DEBUG_LOCATION,
        GRPC_CLOSURE_CREATE(
            [](void* arg, grpc_error_handle /*error*/) {
              auto* chand = static_cast<ClientChannel*>(arg);
              chand->work_serializer_->Run(
                  [chand]()
                      ABSL_EXCLUSIVE_LOCKS_REQUIRED(chand->work_serializer_) {
                        chand->CheckConnectivityState(/*try_to_connect=*/true);
                        GRPC_CHANNEL_STACK_UNREF(chand->owning_stack_,
                                                 "CheckResolutionLocked");
                      },
                  DEBUG_LOCATION);
            },
            chand, nullptr),
        GRPC_ERROR_NONE);
  }
  // Get send_initial_metadata batch and flags.
  auto& send_initial_metadata =
      pending_batches_[0]->payload->send_initial_metadata;
  grpc_metadata_batch* initial_metadata_batch =
      send_initial_metadata.send_initial_metadata;
  const uint32_t send_initial_metadata_flags =
      send_initial_metadata.send_initial_metadata_flags;
  // If we don't yet have a resolver result, we need to queue the call
  // until we get one.
  if (GPR_UNLIKELY(!chand->received_service_config_data_)) {
    // If the resolver returned transient failure before returning the
    // first service config, fail any non-wait_for_ready calls.
    absl::Status resolver_error = chand->resolver_transient_failure_error_;
    if (!resolver_error.ok() && (send_initial_metadata_flags &
                                 GRPC_INITIAL_METADATA_WAIT_FOR_READY) == 0) {
      MaybeRemoveCallFromResolverQueuedCallsLocked(elem);
      *error = absl_status_to_grpc_error(resolver_error);
      return true;
    }
    // Either the resolver has not yet returned a result, or it has
    // returned transient failure but the call is wait_for_ready.  In
    // either case, queue the call.
    MaybeAddCallToResolverQueuedCallsLocked(elem);
    return false;
  }
  // Apply service config to call if not yet applied.
  if (GPR_LIKELY(!service_config_applied_)) {
    service_config_applied_ = true;
    *error = ApplyServiceConfigToCallLocked(elem, initial_metadata_batch);
  }
  MaybeRemoveCallFromResolverQueuedCallsLocked(elem);
  return true;
}

void ClientChannel::CallData::CreateDynamicCall(grpc_call_element* elem) {
  auto* chand = static_cast<ClientChannel*>(elem->channel_data);
  DynamicFilters::Call::Args args = {std::move(dynamic_filters_),
                                     pollent_,
                                     path_,
                                     call_start_time_,
                                     deadline_,
                                     arena_,
                                     call_context_,
                                     call_combiner_};
  grpc_error_handle error = GRPC_ERROR_NONE;
  DynamicFilters* channel_stack = args.channel_stack.get();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(
        GPR_INFO,
        "chand=%p calld=%p: creating dynamic call stack on channel_stack=%p",
        chand, this, channel_stack);
  }
  dynamic_call_ = channel_stack->CreateCall(std::move(args), &error);
  if (error != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: failed to create dynamic call: error=%s",
              chand, this, grpc_error_std_string(error).c_str());
    }
    PendingBatchesFail(elem, error, YieldCallCombiner);
    return;
  }
  PendingBatchesResume(elem);
}

//
// ClientChannel::LoadBalancedCall::Metadata
//

class ClientChannel::LoadBalancedCall::Metadata
    : public LoadBalancingPolicy::MetadataInterface {
 public:
  explicit Metadata(grpc_metadata_batch* batch) : batch_(batch) {}

  void Add(absl::string_view key, absl::string_view value) override {
    // Gross, egregious hack to support legacy grpclb behavior.
    // TODO(ctiller): Use a promise context for this once that plumbing is done.
    if (key == GrpcLbClientStatsMetadata::key()) {
      batch_->Set(
          GrpcLbClientStatsMetadata(),
          const_cast<GrpcLbClientStats*>(
              reinterpret_cast<const GrpcLbClientStats*>(value.data())));
      return;
    }
    batch_->Append(key, Slice::FromStaticString(value),
                   [key](absl::string_view error, const Slice& value) {
                     gpr_log(GPR_ERROR, "%s",
                             absl::StrCat(error, " key:", key,
                                          " value:", value.as_string_view())
                                 .c_str());
                   });
  }

  std::vector<std::pair<std::string, std::string>> TestOnlyCopyToVector()
      override {
    Encoder encoder;
    batch_->Encode(&encoder);
    return encoder.Take();
  }

  absl::optional<absl::string_view> Lookup(absl::string_view key,
                                           std::string* buffer) const override {
    return batch_->GetStringValue(key, buffer);
  }

 private:
  class Encoder {
   public:
    void Encode(const Slice& key, const Slice& value) {
      out_.emplace_back(std::string(key.as_string_view()),
                        std::string(value.as_string_view()));
    }

    template <class Which>
    void Encode(Which, const typename Which::ValueType& value) {
      auto value_slice = Which::Encode(value);
      out_.emplace_back(std::string(Which::key()),
                        std::string(value_slice.as_string_view()));
    }

    void Encode(GrpcTimeoutMetadata, grpc_millis) {}
    void Encode(HttpPathMetadata, const Slice&) {}
    void Encode(HttpMethodMetadata,
                const typename HttpMethodMetadata::ValueType&) {}

    std::vector<std::pair<std::string, std::string>> Take() {
      return std::move(out_);
    }

   private:
    std::vector<std::pair<std::string, std::string>> out_;
  };

  grpc_metadata_batch* batch_;
};

//
// ClientChannel::LoadBalancedCall::LbCallState
//

class ClientChannel::LoadBalancedCall::LbCallState
    : public LoadBalancingPolicy::CallState {
 public:
  explicit LbCallState(LoadBalancedCall* lb_call) : lb_call_(lb_call) {}

  void* Alloc(size_t size) override { return lb_call_->arena_->Alloc(size); }

  absl::string_view ExperimentalGetCallAttribute(const char* key) override {
    auto* service_config_call_data = static_cast<ServiceConfigCallData*>(
        lb_call_->call_context_[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
    auto& call_attributes = service_config_call_data->call_attributes();
    auto it = call_attributes.find(key);
    if (it == call_attributes.end()) return absl::string_view();
    return it->second;
  }

 private:
  LoadBalancedCall* lb_call_;
};

//
// ClientChannel::LoadBalancedCall::BackendMetricAccessor
//

class ClientChannel::LoadBalancedCall::BackendMetricAccessor
    : public LoadBalancingPolicy::BackendMetricAccessor {
 public:
  explicit BackendMetricAccessor(LoadBalancedCall* lb_call)
      : lb_call_(lb_call) {}

  const BackendMetricData* GetBackendMetricData() override {
    if (lb_call_->backend_metric_data_ == nullptr) {
      if (const auto* md = lb_call_->recv_trailing_metadata_->get_pointer(
              XEndpointLoadMetricsBinMetadata())) {
        lb_call_->backend_metric_data_ =
            ParseBackendMetricData(*md, lb_call_->arena_);
      }
    }
    return lb_call_->backend_metric_data_;
  }

 private:
  LoadBalancedCall* lb_call_;
};

//
// ClientChannel::LoadBalancedCall
//

namespace {

CallTracer::CallAttemptTracer* GetCallAttemptTracer(
    grpc_call_context_element* context, bool is_transparent_retry) {
  auto* call_tracer =
      static_cast<CallTracer*>(context[GRPC_CONTEXT_CALL_TRACER].value);
  if (call_tracer == nullptr) return nullptr;
  return call_tracer->StartNewAttempt(is_transparent_retry);
}

}  // namespace

ClientChannel::LoadBalancedCall::LoadBalancedCall(
    ClientChannel* chand, const grpc_call_element_args& args,
    grpc_polling_entity* pollent, grpc_closure* on_call_destruction_complete,
    ConfigSelector::CallDispatchController* call_dispatch_controller,
    bool is_transparent_retry)
    : InternallyRefCounted(
          GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)
              ? "LoadBalancedCall"
              : nullptr),
      chand_(chand),
      path_(grpc_slice_ref_internal(args.path)),
      deadline_(args.deadline),
      arena_(args.arena),
      owning_call_(args.call_stack),
      call_combiner_(args.call_combiner),
      call_context_(args.context),
      pollent_(pollent),
      on_call_destruction_complete_(on_call_destruction_complete),
      call_dispatch_controller_(call_dispatch_controller),
      call_attempt_tracer_(
          GetCallAttemptTracer(args.context, is_transparent_retry)) {}

ClientChannel::LoadBalancedCall::~LoadBalancedCall() {
  GRPC_ERROR_UNREF(cancel_error_);
  GRPC_ERROR_UNREF(failure_error_);
  if (backend_metric_data_ != nullptr) {
    backend_metric_data_->LoadBalancingPolicy::BackendMetricAccessor::
        BackendMetricData::~BackendMetricData();
  }
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i] == nullptr);
  }
  if (on_call_destruction_complete_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_call_destruction_complete_,
                 GRPC_ERROR_NONE);
  }
}

void ClientChannel::LoadBalancedCall::Orphan() {
  // Compute latency and report it to the tracer.
  if (call_attempt_tracer_ != nullptr) {
    gpr_timespec latency =
        gpr_cycle_counter_sub(gpr_get_cycle_counter(), lb_call_start_time_);
    call_attempt_tracer_->RecordEnd(latency);
  }
  Unref();
}

size_t ClientChannel::LoadBalancedCall::GetBatchIndex(
    grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in PickSubchannelLocked() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::LoadBalancedCall::PendingBatchesAdd(
    grpc_transport_stream_op_batch* batch) {
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: adding pending batch at index %" PRIuPTR,
            chand_, this, idx);
  }
  GPR_ASSERT(pending_batches_[idx] == nullptr);
  pending_batches_[idx] = batch;
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::LoadBalancedCall::FailPendingBatchInCallCombiner(
    void* arg, grpc_error_handle error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* self = static_cast<LoadBalancedCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(
      batch, GRPC_ERROR_REF(error), self->call_combiner_);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::LoadBalancedCall::PendingBatchesFail(
    grpc_error_handle error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(failure_error_);
  failure_error_ = error;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: failing %" PRIuPTR " pending batches: %s",
            chand_, this, num_batches, grpc_error_std_string(error).c_str());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        FailPendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_REF(error),
                   "PendingBatchesFail");
      batch = nullptr;
    }
  }
  if (yield_call_combiner_predicate(closures)) {
    closures.RunClosures(call_combiner_);
  } else {
    closures.RunClosuresWithoutYielding(call_combiner_);
  }
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::LoadBalancedCall::ResumePendingBatchInCallCombiner(
    void* arg, grpc_error_handle /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  SubchannelCall* subchannel_call =
      static_cast<SubchannelCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  subchannel_call->StartTransportStreamOpBatch(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::LoadBalancedCall::PendingBatchesResume() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: starting %" PRIuPTR
            " pending batches on subchannel_call=%p",
            chand_, this, num_batches, subchannel_call_.get());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = subchannel_call_.get();
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        ResumePendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, GRPC_ERROR_NONE,
                   "resuming pending batch from LB call");
      batch = nullptr;
    }
  }
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner_);
}

void ClientChannel::LoadBalancedCall::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  // Handle call tracing.
  if (call_attempt_tracer_ != nullptr) {
    // Record send ops in tracer.
    if (batch->cancel_stream) {
      call_attempt_tracer_->RecordCancel(
          GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error));
    }
    if (batch->send_initial_metadata) {
      call_attempt_tracer_->RecordSendInitialMetadata(
          batch->payload->send_initial_metadata.send_initial_metadata,
          batch->payload->send_initial_metadata.send_initial_metadata_flags);
      peer_string_ = batch->payload->send_initial_metadata.peer_string;
      original_send_initial_metadata_on_complete_ = batch->on_complete;
      GRPC_CLOSURE_INIT(&send_initial_metadata_on_complete_,
                        SendInitialMetadataOnComplete, this, nullptr);
      batch->on_complete = &send_initial_metadata_on_complete_;
    }
    if (batch->send_message) {
      call_attempt_tracer_->RecordSendMessage(
          *batch->payload->send_message.send_message);
    }
    if (batch->send_trailing_metadata) {
      call_attempt_tracer_->RecordSendTrailingMetadata(
          batch->payload->send_trailing_metadata.send_trailing_metadata);
    }
    // Intercept recv ops.
    if (batch->recv_initial_metadata) {
      recv_initial_metadata_ =
          batch->payload->recv_initial_metadata.recv_initial_metadata;
      original_recv_initial_metadata_ready_ =
          batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
      GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                        this, nullptr);
      batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
          &recv_initial_metadata_ready_;
    }
    if (batch->recv_message) {
      recv_message_ = batch->payload->recv_message.recv_message;
      original_recv_message_ready_ =
          batch->payload->recv_message.recv_message_ready;
      GRPC_CLOSURE_INIT(&recv_message_ready_, RecvMessageReady, this, nullptr);
      batch->payload->recv_message.recv_message_ready = &recv_message_ready_;
    }
  }
  // Intercept recv_trailing_metadata even if there is no call tracer,
  // since we may need to notify the LB policy about trailing metadata.
  if (batch->recv_trailing_metadata) {
    recv_trailing_metadata_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata;
    transport_stream_stats_ =
        batch->payload->recv_trailing_metadata.collect_stats;
    original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                      this, nullptr);
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }
  // If we've already gotten a subchannel call, pass the batch down to it.
  // Note that once we have picked a subchannel, we do not need to acquire
  // the channel's data plane mutex, which is more efficient (especially for
  // streaming calls).
  if (subchannel_call_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p lb_call=%p: starting batch on subchannel_call=%p",
              chand_, this, subchannel_call_.get());
    }
    subchannel_call_->StartTransportStreamOpBatch(batch);
    return;
  }
  // We do not yet have a subchannel call.
  //
  // If we've previously been cancelled, immediately fail any new batches.
  if (GPR_UNLIKELY(cancel_error_ != GRPC_ERROR_NONE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p lb_call=%p: failing batch with error: %s",
              chand_, this, grpc_error_std_string(cancel_error_).c_str());
    }
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancel_error_), call_combiner_);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    GRPC_ERROR_UNREF(cancel_error_);
    cancel_error_ = GRPC_ERROR_REF(batch->payload->cancel_stream.cancel_error);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p lb_call=%p: recording cancel_error=%s",
              chand_, this, grpc_error_std_string(cancel_error_).c_str());
    }
    // Fail all pending batches.
    PendingBatchesFail(GRPC_ERROR_REF(cancel_error_), NoYieldCallCombiner);
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, GRPC_ERROR_REF(cancel_error_), call_combiner_);
    return;
  }
  // Add the batch to the pending list.
  PendingBatchesAdd(batch);
  // For batches containing a send_initial_metadata op, acquire the
  // channel's data plane mutex to pick a subchannel.
  if (GPR_LIKELY(batch->send_initial_metadata)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p lb_call=%p: grabbing data plane mutex to perform pick",
              chand_, this);
    }
    PickSubchannel(this, GRPC_ERROR_NONE);
  } else {
    // For all other batches, release the call combiner.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p lb_call=%p: saved batch, yielding call combiner",
              chand_, this);
    }
    GRPC_CALL_COMBINER_STOP(call_combiner_,
                            "batch does not include send_initial_metadata");
  }
}

void ClientChannel::LoadBalancedCall::SendInitialMetadataOnComplete(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<LoadBalancedCall*>(arg);
  self->call_attempt_tracer_->RecordOnDoneSendInitialMetadata(
      self->peer_string_);
  Closure::Run(DEBUG_LOCATION,
               self->original_send_initial_metadata_on_complete_,
               GRPC_ERROR_REF(error));
}

void ClientChannel::LoadBalancedCall::RecvInitialMetadataReady(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<LoadBalancedCall*>(arg);
  if (error == GRPC_ERROR_NONE) {
    // recv_initial_metadata_flags is not populated for clients
    self->call_attempt_tracer_->RecordReceivedInitialMetadata(
        self->recv_initial_metadata_, 0 /* recv_initial_metadata_flags */);
  }
  Closure::Run(DEBUG_LOCATION, self->original_recv_initial_metadata_ready_,
               GRPC_ERROR_REF(error));
}

void ClientChannel::LoadBalancedCall::RecvMessageReady(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<LoadBalancedCall*>(arg);
  if (*self->recv_message_ != nullptr) {
    self->call_attempt_tracer_->RecordReceivedMessage(**self->recv_message_);
  }
  Closure::Run(DEBUG_LOCATION, self->original_recv_message_ready_,
               GRPC_ERROR_REF(error));
}

void ClientChannel::LoadBalancedCall::RecvTrailingMetadataReady(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<LoadBalancedCall*>(arg);
  // Check if we have a tracer or an LB callback to invoke.
  if (self->call_attempt_tracer_ != nullptr ||
      self->lb_subchannel_call_tracker_ != nullptr) {
    // Get the call's status.
    absl::Status status;
    if (error != GRPC_ERROR_NONE) {
      // Get status from error.
      grpc_status_code code;
      std::string message;
      grpc_error_get_status(error, self->deadline_, &code, &message,
                            /*http_error=*/nullptr, /*error_string=*/nullptr);
      status = absl::Status(static_cast<absl::StatusCode>(code), message);
    } else {
      // Get status from headers.
      const auto& md = *self->recv_trailing_metadata_;
      grpc_status_code code =
          md.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN);
      if (code != GRPC_STATUS_OK) {
        absl::string_view message;
        if (const auto* grpc_message = md.get_pointer(GrpcMessageMetadata())) {
          message = grpc_message->as_string_view();
        }
        status = absl::Status(static_cast<absl::StatusCode>(code), message);
      }
    }
    // If we have a tracer, notify it.
    if (self->call_attempt_tracer_ != nullptr) {
      self->call_attempt_tracer_->RecordReceivedTrailingMetadata(
          status, self->recv_trailing_metadata_,
          *self->transport_stream_stats_);
    }
    // If the LB policy requested a callback for trailing metadata, invoke
    // the callback.
    if (self->lb_subchannel_call_tracker_ != nullptr) {
      Metadata trailing_metadata(self->recv_trailing_metadata_);
      BackendMetricAccessor backend_metric_accessor(self);
      LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
          status, &trailing_metadata, &backend_metric_accessor};
      self->lb_subchannel_call_tracker_->Finish(args);
      self->lb_subchannel_call_tracker_.reset();
    }
  }
  // Chain to original callback.
  if (self->failure_error_ != GRPC_ERROR_NONE) {
    error = self->failure_error_;
    self->failure_error_ = GRPC_ERROR_NONE;
  } else {
    error = GRPC_ERROR_REF(error);
  }
  Closure::Run(DEBUG_LOCATION, self->original_recv_trailing_metadata_ready_,
               error);
}

void ClientChannel::LoadBalancedCall::CreateSubchannelCall() {
  SubchannelCall::Args call_args = {
      std::move(connected_subchannel_), pollent_, path_.Ref(), /*start_time=*/0,
      deadline_, arena_,
      // TODO(roth): When we implement hedging support, we will probably
      // need to use a separate call context for each subchannel call.
      call_context_, call_combiner_};
  grpc_error_handle error = GRPC_ERROR_NONE;
  subchannel_call_ = SubchannelCall::Create(std::move(call_args), &error);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: create subchannel_call=%p: error=%s", chand_,
            this, subchannel_call_.get(), grpc_error_std_string(error).c_str());
  }
  if (on_call_destruction_complete_ != nullptr) {
    subchannel_call_->SetAfterCallStackDestroy(on_call_destruction_complete_);
    on_call_destruction_complete_ = nullptr;
  }
  if (GPR_UNLIKELY(error != GRPC_ERROR_NONE)) {
    PendingBatchesFail(error, YieldCallCombiner);
  } else {
    PendingBatchesResume();
  }
}

// A class to handle the call combiner cancellation callback for a
// queued pick.
// TODO(roth): When we implement hedging support, we won't be able to
// register a call combiner cancellation closure for each LB pick,
// because there may be multiple LB picks happening in parallel.
// Instead, we will probably need to maintain a list in the CallData
// object of pending LB picks to be cancelled when the closure runs.
class ClientChannel::LoadBalancedCall::LbQueuedCallCanceller {
 public:
  explicit LbQueuedCallCanceller(RefCountedPtr<LoadBalancedCall> lb_call)
      : lb_call_(std::move(lb_call)) {
    GRPC_CALL_STACK_REF(lb_call_->owning_call_, "LbQueuedCallCanceller");
    GRPC_CLOSURE_INIT(&closure_, &CancelLocked, this, nullptr);
    lb_call_->call_combiner_->SetNotifyOnCancel(&closure_);
  }

 private:
  static void CancelLocked(void* arg, grpc_error_handle error) {
    auto* self = static_cast<LbQueuedCallCanceller*>(arg);
    auto* lb_call = self->lb_call_.get();
    auto* chand = lb_call->chand_;
    {
      MutexLock lock(&chand->data_plane_mu_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p lb_call=%p: cancelling queued pick: "
                "error=%s self=%p calld->pick_canceller=%p",
                chand, lb_call, grpc_error_std_string(error).c_str(), self,
                lb_call->lb_call_canceller_);
      }
      if (lb_call->lb_call_canceller_ == self && error != GRPC_ERROR_NONE) {
        lb_call->call_dispatch_controller_->Commit();
        // Remove pick from list of queued picks.
        lb_call->MaybeRemoveCallFromLbQueuedCallsLocked();
        // Fail pending batches on the call.
        lb_call->PendingBatchesFail(GRPC_ERROR_REF(error),
                                    YieldCallCombinerIfPendingBatchesFound);
      }
    }
    GRPC_CALL_STACK_UNREF(lb_call->owning_call_, "LbQueuedCallCanceller");
    delete self;
  }

  RefCountedPtr<LoadBalancedCall> lb_call_;
  grpc_closure closure_;
};

void ClientChannel::LoadBalancedCall::MaybeRemoveCallFromLbQueuedCallsLocked() {
  if (!queued_pending_lb_pick_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p lb_call=%p: removing from queued picks list",
            chand_, this);
  }
  chand_->RemoveLbQueuedCall(&queued_call_, pollent_);
  queued_pending_lb_pick_ = false;
  // Lame the call combiner canceller.
  lb_call_canceller_ = nullptr;
}

void ClientChannel::LoadBalancedCall::MaybeAddCallToLbQueuedCallsLocked() {
  if (queued_pending_lb_pick_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
    gpr_log(GPR_INFO, "chand=%p lb_call=%p: adding to queued picks list",
            chand_, this);
  }
  queued_pending_lb_pick_ = true;
  queued_call_.lb_call = this;
  chand_->AddLbQueuedCall(&queued_call_, pollent_);
  // Register call combiner cancellation callback.
  lb_call_canceller_ = new LbQueuedCallCanceller(Ref());
}

void ClientChannel::LoadBalancedCall::AsyncPickDone(grpc_error_handle error) {
  // TODO(roth): Does this callback need to hold a ref to LoadBalancedCall?
  GRPC_CLOSURE_INIT(&pick_closure_, PickDone, this, grpc_schedule_on_exec_ctx);
  ExecCtx::Run(DEBUG_LOCATION, &pick_closure_, error);
}

void ClientChannel::LoadBalancedCall::PickDone(void* arg,
                                               grpc_error_handle error) {
  auto* self = static_cast<LoadBalancedCall*>(arg);
  if (error != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p lb_call=%p: failed to pick subchannel: error=%s",
              self->chand_, self, grpc_error_std_string(error).c_str());
    }
    self->PendingBatchesFail(GRPC_ERROR_REF(error), YieldCallCombiner);
    return;
  }
  self->call_dispatch_controller_->Commit();
  self->CreateSubchannelCall();
}

void ClientChannel::LoadBalancedCall::PickSubchannel(void* arg,
                                                     grpc_error_handle error) {
  auto* self = static_cast<LoadBalancedCall*>(arg);
  bool pick_complete;
  {
    MutexLock lock(&self->chand_->data_plane_mu_);
    pick_complete = self->PickSubchannelLocked(&error);
  }
  if (pick_complete) {
    PickDone(self, error);
    GRPC_ERROR_UNREF(error);
  }
}

bool ClientChannel::LoadBalancedCall::PickSubchannelLocked(
    grpc_error_handle* error) {
  GPR_ASSERT(connected_subchannel_ == nullptr);
  GPR_ASSERT(subchannel_call_ == nullptr);
  // Grab initial metadata.
  auto& send_initial_metadata =
      pending_batches_[0]->payload->send_initial_metadata;
  grpc_metadata_batch* initial_metadata_batch =
      send_initial_metadata.send_initial_metadata;
  const uint32_t send_initial_metadata_flags =
      send_initial_metadata.send_initial_metadata_flags;
  // Perform LB pick.
  LoadBalancingPolicy::PickArgs pick_args;
  pick_args.path = path_.as_string_view();
  LbCallState lb_call_state(this);
  pick_args.call_state = &lb_call_state;
  Metadata initial_metadata(initial_metadata_batch);
  pick_args.initial_metadata = &initial_metadata;
  auto result = chand_->picker_->Pick(pick_args);
  return HandlePickResult<bool>(
      &result,
      // CompletePick
      [this](LoadBalancingPolicy::PickResult::Complete* complete_pick)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::data_plane_mu_) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
              gpr_log(GPR_INFO,
                      "chand=%p lb_call=%p: LB pick succeeded: subchannel=%p",
                      chand_, this, complete_pick->subchannel.get());
            }
            GPR_ASSERT(complete_pick->subchannel != nullptr);
            // Grab a ref to the connected subchannel while we're still
            // holding the data plane mutex.
            SubchannelWrapper* subchannel = static_cast<SubchannelWrapper*>(
                complete_pick->subchannel.get());
            connected_subchannel_ = subchannel->connected_subchannel();
            // If the subchannel has no connected subchannel (e.g., if the
            // subchannel has moved out of state READY but the LB policy hasn't
            // yet seen that change and given us a new picker), then just
            // queue the pick.  We'll try again as soon as we get a new picker.
            if (connected_subchannel_ == nullptr) {
              if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
                gpr_log(GPR_INFO,
                        "chand=%p lb_call=%p: subchannel returned by LB picker "
                        "has no connected subchannel; queueing pick",
                        chand_, this);
              }
              MaybeAddCallToLbQueuedCallsLocked();
              return false;
            }
            lb_subchannel_call_tracker_ =
                std::move(complete_pick->subchannel_call_tracker);
            if (lb_subchannel_call_tracker_ != nullptr) {
              lb_subchannel_call_tracker_->Start();
            }
            MaybeRemoveCallFromLbQueuedCallsLocked();
            return true;
          },
      // QueuePick
      [this](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::data_plane_mu_) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
              gpr_log(GPR_INFO, "chand=%p lb_call=%p: LB pick queued", chand_,
                      this);
            }
            MaybeAddCallToLbQueuedCallsLocked();
            return false;
          },
      // FailPick
      [this, send_initial_metadata_flags,
       &error](LoadBalancingPolicy::PickResult::Fail* fail_pick)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::data_plane_mu_) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
              gpr_log(GPR_INFO, "chand=%p lb_call=%p: LB pick failed: %s",
                      chand_, this, fail_pick->status.ToString().c_str());
            }
            // If wait_for_ready is false, then the error indicates the RPC
            // attempt's final status.
            if ((send_initial_metadata_flags &
                 GRPC_INITIAL_METADATA_WAIT_FOR_READY) == 0) {
              grpc_error_handle lb_error =
                  absl_status_to_grpc_error(fail_pick->status);
              *error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                  "Failed to pick subchannel", &lb_error, 1);
              GRPC_ERROR_UNREF(lb_error);
              MaybeRemoveCallFromLbQueuedCallsLocked();
              return true;
            }
            // If wait_for_ready is true, then queue to retry when we get a new
            // picker.
            MaybeAddCallToLbQueuedCallsLocked();
            return false;
          },
      // DropPick
      [this, &error](LoadBalancingPolicy::PickResult::Drop* drop_pick)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::data_plane_mu_) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_routing_trace)) {
              gpr_log(GPR_INFO, "chand=%p lb_call=%p: LB pick dropped: %s",
                      chand_, this, drop_pick->status.ToString().c_str());
            }
            *error =
                grpc_error_set_int(absl_status_to_grpc_error(drop_pick->status),
                                   GRPC_ERROR_INT_LB_POLICY_DROP, 1);
            MaybeRemoveCallFromLbQueuedCallsLocked();
            return true;
          });
}

}  // namespace grpc_core
