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

#include <algorithm>
#include <functional>
#include <new>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/backend_metric.h"
#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/client_channel_service_config.h"
#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/dynamic_filters.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/retry_filter.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_interface_internal.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/handshaker/proxy_mapper_registry.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"

//
// Client channel filter
//

namespace grpc_core {

using internal::ClientChannelMethodParsedConfig;

TraceFlag grpc_client_channel_trace(false, "client_channel");
TraceFlag grpc_client_channel_call_trace(false, "client_channel_call");
TraceFlag grpc_client_channel_lb_call_trace(false, "client_channel_lb_call");

//
// ClientChannel::CallData definition
//

class ClientChannel::CallData {
 public:
  // Removes the call from the channel's list of calls queued
  // for name resolution.
  void RemoveCallFromResolverQueuedCallsLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);

  // Called by the channel for each queued call when a new resolution
  // result becomes available.
  virtual void RetryCheckResolutionLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_) = 0;

  RefCountedPtr<DynamicFilters> dynamic_filters() const {
    return dynamic_filters_;
  }

 protected:
  CallData() = default;
  virtual ~CallData() = default;

  // Checks whether a resolver result is available.  The following
  // outcomes are possible:
  // - No resolver result is available yet.  The call will be queued and
  //   absl::nullopt will be returned.  Later, when a resolver result
  //   becomes available, RetryCheckResolutionLocked() will be called.
  // - The resolver has returned a transient failure.  If the call is
  //   not wait_for_ready, a non-OK status will be returned.  (If the
  //   call *is* wait_for_ready, it will be queued instead.)
  // - There is a valid resolver result.  The service config will be
  //   stored in the call context and an OK status will be returned.
  absl::optional<absl::Status> CheckResolution(bool was_queued);

 private:
  // Accessors for data stored in the subclass.
  virtual ClientChannel* chand() const = 0;
  virtual Arena* arena() const = 0;
  virtual grpc_polling_entity* pollent() = 0;
  virtual grpc_metadata_batch* send_initial_metadata() = 0;
  virtual grpc_call_context_element* call_context() const = 0;

  // Helper function for CheckResolution().  Returns true if the call
  // can continue (i.e., there is a valid resolution result, or there is
  // an invalid resolution result but the call is not wait_for_ready).
  bool CheckResolutionLocked(
      absl::StatusOr<RefCountedPtr<ConfigSelector>>* config_selector)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);

  // Adds the call to the channel's list of calls queued for name resolution.
  void AddCallToResolverQueuedCallsLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);

  // Called when adding the call to the resolver queue.
  virtual void OnAddToQueueLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_) {}

  // Applies service config to the call.  Must be invoked once we know
  // that the resolver has returned results to the channel.
  // If an error is returned, the error indicates the status with which
  // the call should be failed.
  grpc_error_handle ApplyServiceConfigToCallLocked(
      const absl::StatusOr<RefCountedPtr<ConfigSelector>>& config_selector);

  // Called to reset the deadline based on the service config obtained
  // from the resolver.
  virtual void ResetDeadline(Duration timeout) = 0;

  RefCountedPtr<DynamicFilters> dynamic_filters_;
};

class ClientChannel::FilterBasedCallData : public ClientChannel::CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* final_info,
                      grpc_closure* then_schedule_closure);
  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);
  static void SetPollent(grpc_call_element* elem, grpc_polling_entity* pollent);

 private:
  class ResolverQueuedCallCanceller;

  FilterBasedCallData(grpc_call_element* elem,
                      const grpc_call_element_args& args);
  ~FilterBasedCallData() override;

  grpc_call_element* elem() const { return deadline_state_.elem; }
  grpc_call_stack* owning_call() const { return deadline_state_.call_stack; }
  CallCombiner* call_combiner() const { return deadline_state_.call_combiner; }

  ClientChannel* chand() const override {
    return static_cast<ClientChannel*>(elem()->channel_data);
  }
  Arena* arena() const override { return deadline_state_.arena; }
  grpc_polling_entity* pollent() override { return pollent_; }
  grpc_metadata_batch* send_initial_metadata() override {
    return pending_batches_[0]
        ->payload->send_initial_metadata.send_initial_metadata;
  }
  grpc_call_context_element* call_context() const override {
    return call_context_;
  }

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
  // Resumes all pending batches on dynamic_call_.
  void PendingBatchesResume();

  // Called to check for a resolution result, both when the call is
  // initially started and when it is queued and the channel gets a new
  // resolution result.
  void TryCheckResolution(bool was_queued);

  void OnAddToQueueLocked() override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);

  void RetryCheckResolutionLocked() override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_);

  void ResetDeadline(Duration timeout) override {
    const Timestamp per_method_deadline =
        Timestamp::FromCycleCounterRoundUp(call_start_time_) + timeout;
    if (per_method_deadline < deadline_) {
      deadline_ = per_method_deadline;
      grpc_deadline_state_reset(&deadline_state_, deadline_);
    }
  }

  void CreateDynamicCall();

  static void RecvTrailingMetadataReadyForConfigSelectorCommitCallback(
      void* arg, grpc_error_handle error);

  grpc_slice path_;  // Request path.
  grpc_call_context_element* call_context_;
  gpr_cycle_counter call_start_time_;
  Timestamp deadline_;

  // State for handling deadlines.
  grpc_deadline_state deadline_state_;

  grpc_polling_entity* pollent_ = nullptr;

  // Accessed while holding ClientChannel::resolution_mu_.
  ResolverQueuedCallCanceller* resolver_call_canceller_
      ABSL_GUARDED_BY(&ClientChannel::resolution_mu_) = nullptr;

  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  grpc_closure recv_trailing_metadata_ready_;

  RefCountedPtr<DynamicFilters::Call> dynamic_call_;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the LB call and are not intercepting any of
  // its callbacks).
  grpc_transport_stream_op_batch* pending_batches_[MAX_PENDING_BATCHES] = {};

  // Set when we get a cancel_stream op.
  grpc_error_handle cancel_error_;
};

class ClientChannel::PromiseBasedCallData : public ClientChannel::CallData {
 public:
  explicit PromiseBasedCallData(ClientChannel* chand) : chand_(chand) {}

  ArenaPromise<absl::StatusOr<CallArgs>> MakeNameResolutionPromise(
      CallArgs call_args) {
    pollent_ = NowOrNever(call_args.polling_entity->WaitAndCopy()).value();
    client_initial_metadata_ = std::move(call_args.client_initial_metadata);
    // If we're still in IDLE, we need to start resolving.
    if (GPR_UNLIKELY(chand_->CheckConnectivityState(false) ==
                     GRPC_CHANNEL_IDLE)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: %striggering exit idle", chand_,
                this, Activity::current()->DebugTag().c_str());
      }
      // Bounce into the control plane work serializer to start resolving.
      GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "ExitIdle");
      chand_->work_serializer_->Run(
          [chand = chand_]()
              ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
                chand->CheckConnectivityState(/*try_to_connect=*/true);
                GRPC_CHANNEL_STACK_UNREF(chand->owning_stack_, "ExitIdle");
              },
          DEBUG_LOCATION);
    }
    return [this, call_args = std::move(
                      call_args)]() mutable -> Poll<absl::StatusOr<CallArgs>> {
      auto result = CheckResolution(was_queued_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: %sCheckResolution returns %s",
                chand_, this, Activity::current()->DebugTag().c_str(),
                result.has_value() ? result->ToString().c_str() : "Pending");
      }
      if (!result.has_value()) return Pending{};
      if (!result->ok()) return *result;
      call_args.client_initial_metadata = std::move(client_initial_metadata_);
      return std::move(call_args);
    };
  }

 private:
  ClientChannel* chand() const override { return chand_; }
  Arena* arena() const override { return GetContext<Arena>(); }
  grpc_polling_entity* pollent() override { return &pollent_; }
  grpc_metadata_batch* send_initial_metadata() override {
    return client_initial_metadata_.get();
  }
  grpc_call_context_element* call_context() const override {
    return GetContext<grpc_call_context_element>();
  }

  void OnAddToQueueLocked() override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_) {
    waker_ = Activity::current()->MakeNonOwningWaker();
    was_queued_ = true;
  }

  void RetryCheckResolutionLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ClientChannel::resolution_mu_) override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: RetryCheckResolutionLocked(): %s",
              chand_, this, waker_.ActivityDebugTag().c_str());
    }
    waker_.WakeupAsync();
  }

  void ResetDeadline(Duration timeout) override {
    CallContext* call_context = GetContext<CallContext>();
    const Timestamp per_method_deadline =
        Timestamp::FromCycleCounterRoundUp(call_context->call_start_time()) +
        timeout;
    call_context->UpdateDeadline(per_method_deadline);
  }

  ClientChannel* chand_;
  grpc_polling_entity pollent_;
  ClientMetadataHandle client_initial_metadata_;
  bool was_queued_ = false;
  Waker waker_ ABSL_GUARDED_BY(&ClientChannel::resolution_mu_);
};

//
// Filter vtable
//

const grpc_channel_filter ClientChannel::kFilterVtableWithPromises = {
    ClientChannel::FilterBasedCallData::StartTransportStreamOpBatch,
    ClientChannel::MakeCallPromise,
    ClientChannel::StartTransportOp,
    sizeof(ClientChannel::FilterBasedCallData),
    ClientChannel::FilterBasedCallData::Init,
    ClientChannel::FilterBasedCallData::SetPollent,
    ClientChannel::FilterBasedCallData::Destroy,
    sizeof(ClientChannel),
    ClientChannel::Init,
    grpc_channel_stack_no_post_init,
    ClientChannel::Destroy,
    ClientChannel::GetChannelInfo,
    "client-channel",
};

const grpc_channel_filter ClientChannel::kFilterVtableWithoutPromises = {
    ClientChannel::FilterBasedCallData::StartTransportStreamOpBatch,
    nullptr,
    ClientChannel::StartTransportOp,
    sizeof(ClientChannel::FilterBasedCallData),
    ClientChannel::FilterBasedCallData::Init,
    ClientChannel::FilterBasedCallData::SetPollent,
    ClientChannel::FilterBasedCallData::Destroy,
    sizeof(ClientChannel),
    ClientChannel::Init,
    grpc_channel_stack_no_post_init,
    ClientChannel::Destroy,
    ClientChannel::GetChannelInfo,
    "client-channel",
};

//
// dynamic termination filter
//

namespace {

ClientChannelServiceConfigCallData* GetServiceConfigCallData(
    grpc_call_context_element* context) {
  return static_cast<ClientChannelServiceConfigCallData*>(
      context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
}

class DynamicTerminationFilter {
 public:
  class CallData;

  static const grpc_channel_filter kFilterVtable;

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args) {
    GPR_ASSERT(args->is_last);
    GPR_ASSERT(elem->filter == &kFilterVtable);
    new (elem->channel_data) DynamicTerminationFilter(args->channel_args);
    return absl::OkStatus();
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

  static ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      grpc_channel_element* elem, CallArgs call_args, NextPromiseFactory) {
    auto* chand = static_cast<DynamicTerminationFilter*>(elem->channel_data);
    return chand->chand_->CreateLoadBalancedCallPromise(
        std::move(call_args),
        []() {
          auto* service_config_call_data =
              GetServiceConfigCallData(GetContext<grpc_call_context_element>());
          service_config_call_data->Commit();
        },
        /*is_transparent_retry=*/false);
  }

 private:
  explicit DynamicTerminationFilter(const ChannelArgs& args)
      : chand_(args.GetObject<ClientChannel>()) {}

  ClientChannel* chand_;
};

class DynamicTerminationFilter::CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args) {
    new (elem->call_data) CallData(*args);
    return absl::OkStatus();
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
      ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, absl::OkStatus());
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
        GetServiceConfigCallData(calld->call_context_);
    calld->lb_call_ = client_channel->CreateLoadBalancedCall(
        args, pollent, nullptr,
        [service_config_call_data]() { service_config_call_data->Commit(); },
        /*is_transparent_retry=*/false);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p dynamic_termination_calld=%p: create lb_call=%p", chand,
              client_channel, calld->lb_call_.get());
    }
  }

 private:
  explicit CallData(const grpc_call_element_args& args)
      : path_(CSliceRef(args.path)),
        deadline_(args.deadline),
        arena_(args.arena),
        owning_call_(args.call_stack),
        call_combiner_(args.call_combiner),
        call_context_(args.context) {}

  ~CallData() { CSliceUnref(path_); }

  grpc_slice path_;  // Request path.
  Timestamp deadline_;
  Arena* arena_;
  grpc_call_stack* owning_call_;
  CallCombiner* call_combiner_;
  grpc_call_context_element* call_context_;

  OrphanablePtr<ClientChannel::FilterBasedLoadBalancedCall> lb_call_;
};

const grpc_channel_filter DynamicTerminationFilter::kFilterVtable = {
    DynamicTerminationFilter::CallData::StartTransportStreamOpBatch,
    DynamicTerminationFilter::MakeCallPromise,
    DynamicTerminationFilter::StartTransportOp,
    sizeof(DynamicTerminationFilter::CallData),
    DynamicTerminationFilter::CallData::Init,
    DynamicTerminationFilter::CallData::SetPollent,
    DynamicTerminationFilter::CallData::Destroy,
    sizeof(DynamicTerminationFilter),
    DynamicTerminationFilter::Init,
    grpc_channel_stack_no_post_init,
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "chand=%p: resolver shutdown complete", chand_);
    }
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_, "ResolverResultHandler");
  }

  void ReportResult(Resolver::Result result) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    chand_->OnResolverResultChangedLocked(std::move(result));
  }

 private:
  ClientChannel* chand_;
};

//
// ClientChannel::SubchannelWrapper
//

// This class is a wrapper for Subchannel that hides details of the
// channel's implementation (such as the connected subchannel) from the
// LB policy API.
//
// Note that no synchronization is needed here, because even if the
// underlying subchannel is shared between channels, this wrapper will only
// be used within one channel, so it will always be synchronized by the
// control plane work_serializer.
class ClientChannel::SubchannelWrapper : public SubchannelInterface {
 public:
  SubchannelWrapper(ClientChannel* chand, RefCountedPtr<Subchannel> subchannel)
      : SubchannelInterface(GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)
                                ? "SubchannelWrapper"
                                : nullptr),
        chand_(chand),
        subchannel_(std::move(subchannel)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: creating subchannel wrapper %p for subchannel %p",
              chand, this, subchannel_.get());
    }
    GRPC_CHANNEL_STACK_REF(chand_->owning_stack_, "SubchannelWrapper");
    GPR_DEBUG_ASSERT(chand_->work_serializer_->RunningInWorkSerializer());
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: destroying subchannel wrapper %p for subchannel %p",
              chand_, this, subchannel_.get());
    }
    if (!IsClientChannelSubchannelWrapperWorkSerializerOrphanEnabled()) {
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
    }
    GRPC_CHANNEL_STACK_UNREF(chand_->owning_stack_, "SubchannelWrapper");
  }

  void Orphan() override {
    if (!IsClientChannelSubchannelWrapperWorkSerializerOrphanEnabled()) {
      return;
    }
    // Make sure we clean up the channel's subchannel maps inside the
    // WorkSerializer.
    // Ref held by callback.
    WeakRef(DEBUG_LOCATION, "subchannel map cleanup").release();
    chand_->work_serializer_->Run(
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
          chand_->subchannel_wrappers_.erase(this);
          if (chand_->channelz_node_ != nullptr) {
            auto* subchannel_node = subchannel_->channelz_node();
            if (subchannel_node != nullptr) {
              auto it =
                  chand_->subchannel_refcount_map_.find(subchannel_.get());
              GPR_ASSERT(it != chand_->subchannel_refcount_map_.end());
              --it->second;
              if (it->second == 0) {
                chand_->channelz_node_->RemoveChildSubchannel(
                    subchannel_node->uuid());
                chand_->subchannel_refcount_map_.erase(it);
              }
            }
          }
          WeakUnref(DEBUG_LOCATION, "subchannel map cleanup");
        },
        DEBUG_LOCATION);
  }

  void WatchConnectivityState(
      std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    auto& watcher_wrapper = watcher_map_[watcher.get()];
    GPR_ASSERT(watcher_wrapper == nullptr);
    watcher_wrapper = new WatcherWrapper(std::move(watcher),
                                         Ref(DEBUG_LOCATION, "WatcherWrapper"));
    subchannel_->WatchConnectivityState(
        RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface>(
            watcher_wrapper));
  }

  void CancelConnectivityStateWatch(ConnectivityStateWatcherInterface* watcher)
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    auto it = watcher_map_.find(watcher);
    GPR_ASSERT(it != watcher_map_.end());
    subchannel_->CancelConnectivityStateWatch(it->second);
    watcher_map_.erase(it);
  }

  RefCountedPtr<ConnectedSubchannel> connected_subchannel() const {
    return subchannel_->connected_subchannel();
  }

  void RequestConnection() override { subchannel_->RequestConnection(); }

  void ResetBackoff() override { subchannel_->ResetBackoff(); }

  void AddDataWatcher(std::unique_ptr<DataWatcherInterface> watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    static_cast<InternalSubchannelDataWatcherInterface*>(watcher.get())
        ->SetSubchannel(subchannel_.get());
    GPR_ASSERT(data_watchers_.insert(std::move(watcher)).second);
  }

  void CancelDataWatcher(DataWatcherInterface* watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    auto it = data_watchers_.find(watcher);
    if (it != data_watchers_.end()) data_watchers_.erase(it);
  }

  void ThrottleKeepaliveTime(int new_keepalive_time) {
    subchannel_->ThrottleKeepaliveTime(new_keepalive_time);
  }

 private:
  // This wrapper provides a bridge between the internal Subchannel API
  // and the SubchannelInterface API that we expose to LB policies.
  // It implements Subchannel::ConnectivityStateWatcherInterface and wraps
  // the instance of SubchannelInterface::ConnectivityStateWatcherInterface
  // that was passed in by the LB policy.  We pass an instance of this
  // class to the underlying Subchannel, and when we get updates from
  // the subchannel, we pass those on to the wrapped watcher to return
  // the update to the LB policy.
  //
  // This class handles things like hopping into the WorkSerializer
  // before passing notifications to the LB policy and propagating
  // keepalive information betwen subchannels.
  class WatcherWrapper : public Subchannel::ConnectivityStateWatcherInterface {
   public:
    WatcherWrapper(
        std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
            watcher,
        RefCountedPtr<SubchannelWrapper> parent)
        : watcher_(std::move(watcher)), parent_(std::move(parent)) {}

    ~WatcherWrapper() override {
      if (!IsClientChannelSubchannelWrapperWorkSerializerOrphanEnabled()) {
        auto* parent = parent_.release();  // ref owned by lambda
        parent->chand_->work_serializer_->Run(
            [parent]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                *parent_->chand_->work_serializer_) {
              parent->Unref(DEBUG_LOCATION, "WatcherWrapper");
            },
            DEBUG_LOCATION);
        return;
      }
      parent_.reset(DEBUG_LOCATION, "WatcherWrapper");
    }

    void OnConnectivityStateChange(
        RefCountedPtr<ConnectivityStateWatcherInterface> self,
        grpc_connectivity_state state, const absl::Status& status) override {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: connectivity change for subchannel wrapper %p "
                "subchannel %p; hopping into work_serializer",
                parent_->chand_, parent_.get(), parent_->subchannel_.get());
      }
      self.release();  // Held by callback.
      parent_->chand_->work_serializer_->Run(
          [this, state, status]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
              *parent_->chand_->work_serializer_) {
            ApplyUpdateInControlPlaneWorkSerializer(state, status);
            Unref();
          },
          DEBUG_LOCATION);
    }

    grpc_pollset_set* interested_parties() override {
      return watcher_->interested_parties();
    }

   private:
    void ApplyUpdateInControlPlaneWorkSerializer(grpc_connectivity_state state,
                                                 const absl::Status& status)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(*parent_->chand_->work_serializer_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p: processing connectivity change in work serializer "
                "for subchannel wrapper %p subchannel %p watcher=%p "
                "state=%s status=%s",
                parent_->chand_, parent_.get(), parent_->subchannel_.get(),
                watcher_.get(), ConnectivityStateName(state),
                status.ToString().c_str());
      }
      absl::optional<absl::Cord> keepalive_throttling =
          status.GetPayload(kKeepaliveThrottlingKey);
      if (keepalive_throttling.has_value()) {
        int new_keepalive_time = -1;
        if (absl::SimpleAtoi(std::string(keepalive_throttling.value()),
                             &new_keepalive_time)) {
          if (new_keepalive_time > parent_->chand_->keepalive_time_) {
            parent_->chand_->keepalive_time_ = new_keepalive_time;
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
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
      // Propagate status only in state TF.
      // We specifically want to avoid propagating the status for
      // state IDLE that the real subchannel gave us only for the
      // purpose of keepalive propagation.
      watcher_->OnConnectivityStateChange(
          state,
          state == GRPC_CHANNEL_TRANSIENT_FAILURE ? status : absl::OkStatus());
    }

    std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
        watcher_;
    RefCountedPtr<SubchannelWrapper> parent_;
  };

  // A heterogenous lookup comparator for data watchers that allows
  // unique_ptr keys to be looked up as raw pointers.
  struct DataWatcherLessThan {
    using is_transparent = void;
    bool operator()(const std::unique_ptr<DataWatcherInterface>& p1,
                    const std::unique_ptr<DataWatcherInterface>& p2) const {
      return p1 < p2;
    }
    bool operator()(const std::unique_ptr<DataWatcherInterface>& p1,
                    const DataWatcherInterface* p2) const {
      return p1.get() < p2;
    }
    bool operator()(const DataWatcherInterface* p1,
                    const std::unique_ptr<DataWatcherInterface>& p2) const {
      return p1 < p2.get();
    }
  };

  ClientChannel* chand_;
  RefCountedPtr<Subchannel> subchannel_;
  // Maps from the address of the watcher passed to us by the LB policy
  // to the address of the WrapperWatcher that we passed to the underlying
  // subchannel.  This is needed so that when the LB policy calls
  // CancelConnectivityStateWatch() with its watcher, we know the
  // corresponding WrapperWatcher to cancel on the underlying subchannel.
  std::map<ConnectivityStateWatcherInterface*, WatcherWrapper*> watcher_map_
      ABSL_GUARDED_BY(*chand_->work_serializer_);
  std::set<std::unique_ptr<DataWatcherInterface>, DataWatcherLessThan>
      data_watchers_ ABSL_GUARDED_BY(*chand_->work_serializer_);
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
      [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
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
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, absl::OkStatus());
  // Hop back into the work_serializer to clean up.
  // Not needed in state SHUTDOWN, because the tracker will
  // automatically remove all watchers in that case.
  // Note: The callback takes a ref in case the ref inside the state tracker
  // gets removed before the callback runs via a SHUTDOWN notification.
  if (state != GRPC_CHANNEL_SHUTDOWN) {
    Ref(DEBUG_LOCATION, "RemoveWatcherLocked()").release();
    chand_->work_serializer_->Run(
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
          RemoveWatcherLocked();
          Unref(DEBUG_LOCATION, "RemoveWatcherLocked()");
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
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, absl::CancelledError());
  // Hop back into the work_serializer to clean up.
  // Note: The callback takes a ref in case the ref inside the state tracker
  // gets removed before the callback runs via a SHUTDOWN notification.
  Ref(DEBUG_LOCATION, "RemoveWatcherLocked()").release();
  chand_->work_serializer_->Run(
      [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
        RemoveWatcherLocked();
        Unref(DEBUG_LOCATION, "RemoveWatcherLocked()");
      },
      DEBUG_LOCATION);
}

void ClientChannel::ExternalConnectivityWatcher::AddWatcherLocked() {
  Closure::Run(DEBUG_LOCATION, watcher_timer_init_, absl::OkStatus());
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
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
          AddWatcherLocked();
        },
        DEBUG_LOCATION);
  }

 private:
  void AddWatcherLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
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
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
          RemoveWatcherLocked();
        },
        DEBUG_LOCATION);
  }

 private:
  void RemoveWatcherLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
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
      ServerAddress address, const ChannelArgs& args) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    if (chand_->resolver_ == nullptr) return nullptr;  // Shutting down.
    ChannelArgs subchannel_args = ClientChannel::MakeSubchannelArgs(
        args, address.args(), chand_->subchannel_pool_,
        chand_->default_authority_);
    // Create subchannel.
    RefCountedPtr<Subchannel> subchannel =
        chand_->client_channel_factory_->CreateSubchannel(address.address(),
                                                          subchannel_args);
    if (subchannel == nullptr) return nullptr;
    // Make sure the subchannel has updated keepalive time.
    subchannel->ThrottleKeepaliveTime(chand_->keepalive_time_);
    // Create and return wrapper for the subchannel.
    return MakeRefCounted<SubchannelWrapper>(chand_, std::move(subchannel));
  }

  void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                   RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker)
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    if (chand_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      const char* extra = chand_->disconnect_error_.ok()
                              ? ""
                              : " (ignoring -- channel shutting down)";
      gpr_log(GPR_INFO, "chand=%p: update: state=%s status=(%s) picker=%p%s",
              chand_, ConnectivityStateName(state), status.ToString().c_str(),
              picker.get(), extra);
    }
    // Do update only if not shutting down.
    if (chand_->disconnect_error_.ok()) {
      chand_->UpdateStateAndPickerLocked(state, status, "helper",
                                         std::move(picker));
    }
  }

  void RequestReresolution() override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
    if (chand_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "chand=%p: started name re-resolving", chand_);
    }
    chand_->resolver_->RequestReresolutionLocked();
  }

  absl::string_view GetAuthority() override {
    return chand_->default_authority_;
  }

  RefCountedPtr<grpc_channel_credentials> GetChannelCredentials() override {
    return chand_->channel_args_.GetObject<grpc_channel_credentials>()
        ->duplicate_without_call_credentials();
  }

  RefCountedPtr<grpc_channel_credentials> GetUnsafeChannelCredentials()
      override {
    return chand_->channel_args_.GetObject<grpc_channel_credentials>()->Ref();
  }

  grpc_event_engine::experimental::EventEngine* GetEventEngine() override {
    return chand_->owning_stack_->EventEngine();
  }

  void AddTraceEvent(TraceSeverity severity, absl::string_view message) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand_->work_serializer_) {
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

ClientChannel* ClientChannel::GetFromChannel(Channel* channel) {
  grpc_channel_element* elem =
      grpc_channel_stack_last_element(channel->channel_stack());
  if (elem->filter != &kFilterVtableWithPromises &&
      elem->filter != &kFilterVtableWithoutPromises) {
    return nullptr;
  }
  return static_cast<ClientChannel*>(elem->channel_data);
}

grpc_error_handle ClientChannel::Init(grpc_channel_element* elem,
                                      grpc_channel_element_args* args) {
  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &kFilterVtableWithPromises ||
             elem->filter == &kFilterVtableWithoutPromises);
  grpc_error_handle error;
  new (elem->channel_data) ClientChannel(args, &error);
  return error;
}

void ClientChannel::Destroy(grpc_channel_element* elem) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  chand->~ClientChannel();
}

namespace {

RefCountedPtr<SubchannelPoolInterface> GetSubchannelPool(
    const ChannelArgs& args) {
  if (args.GetBool(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL).value_or(false)) {
    return MakeRefCounted<LocalSubchannelPool>();
  }
  return GlobalSubchannelPool::instance();
}

}  // namespace

ClientChannel::ClientChannel(grpc_channel_element_args* args,
                             grpc_error_handle* error)
    : channel_args_(args->channel_args),
      deadline_checking_enabled_(grpc_deadline_checking_enabled(channel_args_)),
      owning_stack_(args->channel_stack),
      client_channel_factory_(channel_args_.GetObject<ClientChannelFactory>()),
      channelz_node_(channel_args_.GetObject<channelz::ChannelNode>()),
      interested_parties_(grpc_pollset_set_create()),
      service_config_parser_index_(
          internal::ClientChannelServiceConfigParser::ParserIndex()),
      work_serializer_(
          std::make_shared<WorkSerializer>(*args->channel_stack->event_engine)),
      state_tracker_("client_channel", GRPC_CHANNEL_IDLE),
      subchannel_pool_(GetSubchannelPool(channel_args_)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: creating client_channel for channel stack %p",
            this, owning_stack_);
  }
  // Start backup polling.
  grpc_client_channel_start_backup_polling(interested_parties_);
  // Check client channel factory.
  if (client_channel_factory_ == nullptr) {
    *error = GRPC_ERROR_CREATE(
        "Missing client channel factory in args for client channel filter");
    return;
  }
  // Get default service config.  If none is specified via the client API,
  // we use an empty config.
  absl::optional<absl::string_view> service_config_json =
      channel_args_.GetString(GRPC_ARG_SERVICE_CONFIG);
  if (!service_config_json.has_value()) service_config_json = "{}";
  *error = absl::OkStatus();
  auto service_config =
      ServiceConfigImpl::Create(channel_args_, *service_config_json);
  if (!service_config.ok()) {
    *error = absl_status_to_grpc_error(service_config.status());
    return;
  }
  default_service_config_ = std::move(*service_config);
  // Get URI to resolve, using proxy mapper if needed.
  absl::optional<std::string> server_uri =
      channel_args_.GetOwnedString(GRPC_ARG_SERVER_URI);
  if (!server_uri.has_value()) {
    *error = GRPC_ERROR_CREATE(
        "target URI channel arg missing or wrong type in client channel "
        "filter");
    return;
  }
  uri_to_resolve_ = CoreConfiguration::Get()
                        .proxy_mapper_registry()
                        .MapName(*server_uri, &channel_args_)
                        .value_or(*server_uri);
  // Make sure the URI to resolve is valid, so that we know that
  // resolver creation will succeed later.
  if (!CoreConfiguration::Get().resolver_registry().IsValidTarget(
          uri_to_resolve_)) {
    *error = GRPC_ERROR_CREATE(
        absl::StrCat("the target uri is not valid: ", uri_to_resolve_));
    return;
  }
  // Strip out service config channel arg, so that it doesn't affect
  // subchannel uniqueness when the args flow down to that layer.
  channel_args_ = channel_args_.Remove(GRPC_ARG_SERVICE_CONFIG);
  // Set initial keepalive time.
  auto keepalive_arg = channel_args_.GetInt(GRPC_ARG_KEEPALIVE_TIME_MS);
  if (keepalive_arg.has_value()) {
    keepalive_time_ = Clamp(*keepalive_arg, 1, INT_MAX);
  } else {
    keepalive_time_ = -1;  // unset
  }
  // Set default authority.
  absl::optional<std::string> default_authority =
      channel_args_.GetOwnedString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (!default_authority.has_value()) {
    default_authority_ =
        CoreConfiguration::Get().resolver_registry().GetDefaultAuthority(
            *server_uri);
  } else {
    default_authority_ = std::move(*default_authority);
  }
  // Success.
  *error = absl::OkStatus();
}

ClientChannel::~ClientChannel() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: destroying channel", this);
  }
  DestroyResolverAndLbPolicyLocked();
  // Stop backup polling.
  grpc_client_channel_stop_backup_polling(interested_parties_);
  grpc_pollset_set_destroy(interested_parties_);
}

ArenaPromise<ServerMetadataHandle> ClientChannel::MakeCallPromise(
    grpc_channel_element* elem, CallArgs call_args, NextPromiseFactory) {
  auto* chand = static_cast<ClientChannel*>(elem->channel_data);
  // TODO(roth): Is this the right lifetime story for calld?
  auto* calld = GetContext<Arena>()->ManagedNew<PromiseBasedCallData>(chand);
  return TrySeq(
      // Name resolution.
      calld->MakeNameResolutionPromise(std::move(call_args)),
      // Dynamic filter stack.
      [calld](CallArgs call_args) mutable {
        return calld->dynamic_filters()->channel_stack()->MakeClientCallPromise(
            std::move(call_args));
      });
}

OrphanablePtr<ClientChannel::FilterBasedLoadBalancedCall>
ClientChannel::CreateLoadBalancedCall(
    const grpc_call_element_args& args, grpc_polling_entity* pollent,
    grpc_closure* on_call_destruction_complete,
    absl::AnyInvocable<void()> on_commit, bool is_transparent_retry) {
  return OrphanablePtr<FilterBasedLoadBalancedCall>(
      args.arena->New<FilterBasedLoadBalancedCall>(
          this, args, pollent, on_call_destruction_complete,
          std::move(on_commit), is_transparent_retry));
}

ArenaPromise<ServerMetadataHandle> ClientChannel::CreateLoadBalancedCallPromise(
    CallArgs call_args, absl::AnyInvocable<void()> on_commit,
    bool is_transparent_retry) {
  OrphanablePtr<PromiseBasedLoadBalancedCall> lb_call(
      GetContext<Arena>()->New<PromiseBasedLoadBalancedCall>(
          this, std::move(on_commit), is_transparent_retry));
  auto* call_ptr = lb_call.get();
  return call_ptr->MakeCallPromise(std::move(call_args), std::move(lb_call));
}

ChannelArgs ClientChannel::MakeSubchannelArgs(
    const ChannelArgs& channel_args, const ChannelArgs& address_args,
    const RefCountedPtr<SubchannelPoolInterface>& subchannel_pool,
    const std::string& channel_default_authority) {
  // Note that we start with the channel-level args and then apply the
  // per-address args, so that if a value is present in both, the one
  // in the channel-level args is used.  This is particularly important
  // for the GRPC_ARG_DEFAULT_AUTHORITY arg, which we want to allow
  // resolvers to set on a per-address basis only if the application
  // did not explicitly set it at the channel level.
  return channel_args.UnionWith(address_args)
      .SetObject(subchannel_pool)
      // If we haven't already set the default authority arg (i.e., it
      // was not explicitly set by the application nor overridden by
      // the resolver), add it from the channel's default.
      .SetIfUnset(GRPC_ARG_DEFAULT_AUTHORITY, channel_default_authority)
      // Remove channel args that should not affect subchannel
      // uniqueness.
      .Remove(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME)
      .Remove(GRPC_ARG_INHIBIT_HEALTH_CHECKING)
      .Remove(GRPC_ARG_CHANNELZ_CHANNEL_NODE)
      // Remove all keys with the no-subchannel prefix.
      .RemoveAllKeysWithPrefix(GRPC_ARG_NO_SUBCHANNEL_PREFIX);
}

void ClientChannel::ReprocessQueuedResolverCalls() {
  for (CallData* calld : resolver_queued_calls_) {
    calld->RemoveCallFromResolverQueuedCallsLocked();
    calld->RetryCheckResolutionLocked();
  }
  resolver_queued_calls_.clear();
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
  absl::optional<absl::string_view> policy_name;
  if (!parsed_service_config->parsed_deprecated_lb_policy().empty()) {
    policy_name = parsed_service_config->parsed_deprecated_lb_policy();
  } else {
    policy_name = resolver_result.args.GetString(GRPC_ARG_LB_POLICY_NAME);
    bool requires_config = false;
    if (policy_name.has_value() &&
        (!CoreConfiguration::Get()
              .lb_policy_registry()
              .LoadBalancingPolicyExists(*policy_name, &requires_config) ||
         requires_config)) {
      if (requires_config) {
        gpr_log(GPR_ERROR,
                "LB policy: %s passed through channel_args must not "
                "require a config. Using pick_first instead.",
                std::string(*policy_name).c_str());
      } else {
        gpr_log(GPR_ERROR,
                "LB policy: %s passed through channel_args does not exist. "
                "Using pick_first instead.",
                std::string(*policy_name).c_str());
      }
      policy_name = "pick_first";
    }
  }
  // Use pick_first if nothing was specified and we didn't select grpclb
  // above.
  if (!policy_name.has_value()) policy_name = "pick_first";
  // Now that we have the policy name, construct an empty config for it.
  Json config_json = Json::FromArray({Json::FromObject({
      {std::string(*policy_name), Json::FromObject({})},
  })});
  auto lb_policy_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          config_json);
  // The policy name came from one of three places:
  // - The deprecated loadBalancingPolicy field in the service config,
  //   in which case the code in ClientChannelServiceConfigParser
  //   already verified that the policy does not require a config.
  // - One of the hard-coded values here, all of which are known to not
  //   require a config.
  // - A channel arg, in which case we check that the specified policy exists
  //   and accepts an empty config. If not, we revert to using pick_first
  //   lb_policy
  GPR_ASSERT(lb_policy_config.ok());
  return std::move(*lb_policy_config);
}

}  // namespace

void ClientChannel::OnResolverResultChangedLocked(Resolver::Result result) {
  // Handle race conditions.
  if (resolver_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: got resolver result", this);
  }
  // Grab resolver result health callback.
  auto resolver_callback = std::move(result.result_health_callback);
  absl::Status resolver_result_status;
  // We only want to trace the address resolution in the follow cases:
  // (a) Address resolution resulted in service config change.
  // (b) Address resolution that causes number of backends to go from
  //     zero to non-zero.
  // (c) Address resolution that causes number of backends to go from
  //     non-zero to zero.
  // (d) Address resolution that causes a new LB policy to be created.
  //
  // We track a list of strings to eventually be concatenated and traced.
  std::vector<const char*> trace_strings;
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "chand=%p: resolver returned service config error: %s",
              this, result.service_config.status().ToString().c_str());
    }
    // If the service config was invalid, then fallback to the
    // previously returned service config.
    if (saved_service_config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
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
      resolver_result_status =
          absl::UnavailableError("no valid service config");
    }
  } else if (*result.service_config == nullptr) {
    // Resolver did not return any service config.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p: resolver returned no service config. Using default "
              "service config for channel.",
              this);
    }
    service_config = default_service_config_;
  } else {
    // Use ServiceConfig and ConfigSelector returned by resolver.
    service_config = std::move(*result.service_config);
    config_selector = result.args.GetObjectRef<ConfigSelector>();
  }
  // Note: The only case in which service_config is null here is if the resolver
  // returned a service config error and we don't have a previous service
  // config to fall back to.
  if (service_config != nullptr) {
    // Extract global config for client channel.
    const internal::ClientChannelGlobalParsedConfig* parsed_service_config =
        static_cast<const internal::ClientChannelGlobalParsedConfig*>(
            service_config->GetGlobalParsedConfig(
                service_config_parser_index_));
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
      UpdateServiceConfigInControlPlaneLocked(
          std::move(service_config), std::move(config_selector),
          std::string(lb_policy_config->name()));
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "chand=%p: service config not changed", this);
    }
    // Create or update LB policy, as needed.
    resolver_result_status = CreateOrUpdateLbPolicyLocked(
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
  // Invoke resolver callback if needed.
  if (resolver_callback != nullptr) {
    resolver_callback(std::move(resolver_result_status));
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: resolver transient failure: %s", this,
            status.ToString().c_str());
  }
  // If we already have an LB policy from a previous resolution
  // result, then we continue to let it set the connectivity state.
  // Otherwise, we go into TRANSIENT_FAILURE.
  if (lb_policy_ == nullptr) {
    // Update connectivity state.
    UpdateStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                      "resolver failure");
    {
      MutexLock lock(&resolution_mu_);
      // Update resolver transient failure.
      resolver_transient_failure_error_ =
          MaybeRewriteIllegalStatusCode(status, "resolver");
      ReprocessQueuedResolverCalls();
    }
  }
}

absl::Status ClientChannel::CreateOrUpdateLbPolicyLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config,
    const absl::optional<std::string>& health_check_service_name,
    Resolver::Result result) {
  // Construct update.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses = std::move(result.addresses);
  update_args.config = std::move(lb_policy_config);
  update_args.resolution_note = std::move(result.resolution_note);
  // Remove the config selector from channel args so that we're not holding
  // unnecessary refs that cause it to be destroyed somewhere other than in the
  // WorkSerializer.
  update_args.args = result.args.Remove(GRPC_ARG_CONFIG_SELECTOR);
  // Add health check service name to channel args.
  if (health_check_service_name.has_value()) {
    update_args.args = update_args.args.Set(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME,
                                            *health_check_service_name);
  }
  // Create policy if needed.
  if (lb_policy_ == nullptr) {
    lb_policy_ = CreateLbPolicyLocked(update_args.args);
  }
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: Updating child policy %p", this,
            lb_policy_.get());
  }
  return lb_policy_->UpdateLocked(std::move(update_args));
}

// Creates a new LB policy.
OrphanablePtr<LoadBalancingPolicy> ClientChannel::CreateLbPolicyLocked(
    const ChannelArgs& args) {
  // The LB policy will start in state CONNECTING but will not
  // necessarily send us an update synchronously, so set state to
  // CONNECTING (in case the resolver had previously failed and put the
  // channel into TRANSIENT_FAILURE) and make sure we have a queueing picker.
  UpdateStateAndPickerLocked(
      GRPC_CHANNEL_CONNECTING, absl::Status(), "started resolving",
      MakeRefCounted<LoadBalancingPolicy::QueuePicker>(nullptr));
  // Now create the LB policy.
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer_;
  lb_policy_args.channel_control_helper =
      std::make_unique<ClientChannelControlHelper>(this);
  lb_policy_args.args = args;
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_client_channel_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: created new LB policy %p", this,
            lb_policy.get());
  }
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties_);
  return lb_policy;
}

void ClientChannel::UpdateServiceConfigInControlPlaneLocked(
    RefCountedPtr<ServiceConfig> service_config,
    RefCountedPtr<ConfigSelector> config_selector, std::string lb_policy_name) {
  std::string service_config_json(service_config->json_string());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: using service config: \"%s\"", this,
            service_config_json.c_str());
  }
  // Save service config.
  saved_service_config_ = std::move(service_config);
  // Swap out the data used by GetChannelInfo().
  {
    MutexLock lock(&info_mu_);
    info_lb_policy_name_ = std::move(lb_policy_name);
    info_service_config_json_ = std::move(service_config_json);
  }
  // Save config selector.
  saved_config_selector_ = std::move(config_selector);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: using ConfigSelector %p", this,
            saved_config_selector_.get());
  }
}

void ClientChannel::UpdateServiceConfigInDataPlaneLocked() {
  // Grab ref to service config.
  RefCountedPtr<ServiceConfig> service_config = saved_service_config_;
  // Grab ref to config selector.  Use default if resolver didn't supply one.
  RefCountedPtr<ConfigSelector> config_selector = saved_config_selector_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: switching to ConfigSelector %p", this,
            saved_config_selector_.get());
  }
  if (config_selector == nullptr) {
    config_selector =
        MakeRefCounted<DefaultConfigSelector>(saved_service_config_);
  }
  ChannelArgs new_args =
      channel_args_.SetObject(this).SetObject(service_config);
  bool enable_retries =
      !new_args.WantMinimalStack() &&
      new_args.GetBool(GRPC_ARG_ENABLE_RETRIES).value_or(true);
  // Construct dynamic filter stack.
  std::vector<const grpc_channel_filter*> filters =
      config_selector->GetFilters();
  if (enable_retries) {
    filters.push_back(&RetryFilter::kVtable);
  } else {
    filters.push_back(&DynamicTerminationFilter::kFilterVtable);
  }
  RefCountedPtr<DynamicFilters> dynamic_filters =
      DynamicFilters::Create(new_args, std::move(filters));
  GPR_ASSERT(dynamic_filters != nullptr);
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
    // Re-process queued calls asynchronously.
    ReprocessQueuedResolverCalls();
  }
  // Old values will be unreffed after lock is released when they go out
  // of scope.
}

void ClientChannel::CreateResolverLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: starting name resolution for %s", this,
            uri_to_resolve_.c_str());
  }
  resolver_ = CoreConfiguration::Get().resolver_registry().CreateResolver(
      uri_to_resolve_, channel_args_, interested_parties_, work_serializer_,
      std::make_unique<ResolverResultHandler>(this));
  // Since the validity of the args was checked when the channel was created,
  // CreateResolver() must return a non-null result.
  GPR_ASSERT(resolver_ != nullptr);
  UpdateStateLocked(GRPC_CHANNEL_CONNECTING, absl::Status(),
                    "started resolving");
  resolver_->StartLocked();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "chand=%p: created resolver=%p", this, resolver_.get());
  }
}

void ClientChannel::DestroyResolverAndLbPolicyLocked() {
  if (resolver_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "chand=%p: shutting down resolver=%p", this,
              resolver_.get());
    }
    resolver_.reset();
    // Clear resolution state.
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
    // Clear LB policy if set.
    if (lb_policy_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO, "chand=%p: shutting down lb_policy=%p", this,
                lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties_);
      lb_policy_.reset();
    }
  }
}

void ClientChannel::UpdateStateLocked(grpc_connectivity_state state,
                                      const absl::Status& status,
                                      const char* reason) {
  if (state != GRPC_CHANNEL_SHUTDOWN &&
      state_tracker_.state() == GRPC_CHANNEL_SHUTDOWN) {
    Crash("Illegal transition SHUTDOWN -> anything");
  }
  state_tracker_.SetState(state, status, reason);
  if (channelz_node_ != nullptr) {
    channelz_node_->SetConnectivityState(state);
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            channelz::ChannelNode::GetChannelConnectivityStateChangeString(
                state)));
  }
}

void ClientChannel::UpdateStateAndPickerLocked(
    grpc_connectivity_state state, const absl::Status& status,
    const char* reason,
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) {
  UpdateStateLocked(state, status, reason);
  // Grab the LB lock to update the picker and trigger reprocessing of the
  // queued picks.
  // Old picker will be unreffed after releasing the lock.
  MutexLock lock(&lb_mu_);
  picker_.swap(picker);
  // Reprocess queued picks.
  for (auto& call : lb_queued_calls_) {
    call->RemoveCallFromLbQueuedCallsLocked();
    call->RetryPickLocked();
  }
  lb_queued_calls_.clear();
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
    return GRPC_ERROR_CREATE("channel not connected");
  }
  LoadBalancingPolicy::PickResult result;
  {
    MutexLock lock(&lb_mu_);
    result = picker_->Pick(LoadBalancingPolicy::PickArgs());
  }
  return HandlePickResult<grpc_error_handle>(
      &result,
      // Complete pick.
      [op](LoadBalancingPolicy::PickResult::Complete* complete_pick)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(*ClientChannel::work_serializer_) {
            SubchannelWrapper* subchannel = static_cast<SubchannelWrapper*>(
                complete_pick->subchannel.get());
            RefCountedPtr<ConnectedSubchannel> connected_subchannel =
                subchannel->connected_subchannel();
            if (connected_subchannel == nullptr) {
              return GRPC_ERROR_CREATE("LB pick for ping not connected");
            }
            connected_subchannel->Ping(op->send_ping.on_initiate,
                                       op->send_ping.on_ack);
            return absl::OkStatus();
          },
      // Queue pick.
      [](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/) {
        return GRPC_ERROR_CREATE("LB picker queued call");
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
    if (!error.ok()) {
      ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_initiate, error);
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
  if (!op->disconnect_with_error.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "chand=%p: disconnect_with_error: %s", this,
              StatusToString(op->disconnect_with_error).c_str());
    }
    DestroyResolverAndLbPolicyLocked();
    intptr_t value;
    if (grpc_error_get_int(op->disconnect_with_error,
                           StatusIntProperty::ChannelConnectivityState,
                           &value) &&
        static_cast<grpc_connectivity_state>(value) == GRPC_CHANNEL_IDLE) {
      if (disconnect_error_.ok()) {  // Ignore if we're shutting down.
        // Enter IDLE state.
        UpdateStateAndPickerLocked(GRPC_CHANNEL_IDLE, absl::Status(),
                                   "channel entering IDLE", nullptr);
        // TODO(roth): Do we need to check for any queued picks here, in
        // case there's a race condition in the client_idle filter?
        // And maybe also check for calls in the resolver queue?
      }
    } else {
      // Disconnect.
      GPR_ASSERT(disconnect_error_.ok());
      disconnect_error_ = op->disconnect_with_error;
      UpdateStateAndPickerLocked(
          GRPC_CHANNEL_SHUTDOWN, absl::Status(), "shutdown from API",
          MakeRefCounted<LoadBalancingPolicy::TransientFailurePicker>(
              grpc_error_to_absl_status(op->disconnect_with_error)));
      // TODO(roth): If this happens when we're still waiting for a
      // resolver result, we need to trigger failures for all calls in
      // the resolver queue here.
    }
  }
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "start_transport_op");
  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
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
      [chand, op]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand->work_serializer_) {
        chand->StartTransportOpLocked(op);
      },
      DEBUG_LOCATION);
}

void ClientChannel::GetChannelInfo(grpc_channel_element* elem,
                                   const grpc_channel_info* info) {
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  MutexLock lock(&chand->info_mu_);
  if (info->lb_policy_name != nullptr) {
    *info->lb_policy_name = gpr_strdup(chand->info_lb_policy_name_.c_str());
  }
  if (info->service_config_json != nullptr) {
    *info->service_config_json =
        gpr_strdup(chand->info_service_config_json_.c_str());
  }
}

void ClientChannel::TryToConnectLocked() {
  if (disconnect_error_.ok()) {
    if (lb_policy_ != nullptr) {
      lb_policy_->ExitIdleLocked();
    } else if (resolver_ == nullptr) {
      CreateResolverLocked();
    }
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
                              *work_serializer_) { TryToConnectLocked(); },
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

void ClientChannel::CallData::RemoveCallFromResolverQueuedCallsLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: removing from resolver queued picks list",
            chand(), this);
  }
  // Remove call's pollent from channel's interested_parties.
  grpc_polling_entity_del_from_pollset_set(pollent(),
                                           chand()->interested_parties_);
  // Note: There's no need to actually remove the call from the queue
  // here, because that will be done in
  // ResolverQueuedCallCanceller::CancelLocked() or
  // ClientChannel::ReprocessQueuedResolverCalls().
}

void ClientChannel::CallData::AddCallToResolverQueuedCallsLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(
        GPR_INFO,
        "chand=%p calld=%p: adding to resolver queued picks list; pollent=%s",
        chand(), this, grpc_polling_entity_string(pollent()).c_str());
  }
  // Add call's pollent to channel's interested_parties, so that I/O
  // can be done under the call's CQ.
  grpc_polling_entity_add_to_pollset_set(pollent(),
                                         chand()->interested_parties_);
  // Add to queue.
  chand()->resolver_queued_calls_.insert(this);
  OnAddToQueueLocked();
}

grpc_error_handle ClientChannel::CallData::ApplyServiceConfigToCallLocked(
    const absl::StatusOr<RefCountedPtr<ConfigSelector>>& config_selector) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: applying service config to call",
            chand(), this);
  }
  if (!config_selector.ok()) return config_selector.status();
  // Create a ClientChannelServiceConfigCallData for the call.  This stores
  // a ref to the ServiceConfig and caches the right set of parsed configs
  // to use for the call.  The ClientChannelServiceConfigCallData will store
  // itself in the call context, so that it can be accessed by filters
  // below us in the stack, and it will be cleaned up when the call ends.
  auto* service_config_call_data =
      arena()->New<ClientChannelServiceConfigCallData>(arena(), call_context());
  // Use the ConfigSelector to determine the config for the call.
  absl::Status call_config_status =
      (*config_selector)
          ->GetCallConfig(
              {send_initial_metadata(), arena(), service_config_call_data});
  if (!call_config_status.ok()) {
    return absl_status_to_grpc_error(
        MaybeRewriteIllegalStatusCode(call_config_status, "ConfigSelector"));
  }
  // Apply our own method params to the call.
  auto* method_params = static_cast<ClientChannelMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          chand()->service_config_parser_index_));
  if (method_params != nullptr) {
    // If the deadline from the service config is shorter than the one
    // from the client API, reset the deadline timer.
    if (chand()->deadline_checking_enabled_ &&
        method_params->timeout() != Duration::Zero()) {
      ResetDeadline(method_params->timeout());
    }
    // If the service config set wait_for_ready and the application
    // did not explicitly set it, use the value from the service config.
    auto* wait_for_ready =
        send_initial_metadata()->GetOrCreatePointer(WaitForReady());
    if (method_params->wait_for_ready().has_value() &&
        !wait_for_ready->explicitly_set) {
      wait_for_ready->value = method_params->wait_for_ready().value();
    }
  }
  return absl::OkStatus();
}

absl::optional<absl::Status> ClientChannel::CallData::CheckResolution(
    bool was_queued) {
  // Check if we have a resolver result to use.
  absl::StatusOr<RefCountedPtr<ConfigSelector>> config_selector;
  {
    MutexLock lock(&chand()->resolution_mu_);
    bool result_ready = CheckResolutionLocked(&config_selector);
    // If no result is available, queue the call.
    if (!result_ready) {
      AddCallToResolverQueuedCallsLocked();
      return absl::nullopt;
    }
  }
  // We have a result.  Apply service config to call.
  grpc_error_handle error = ApplyServiceConfigToCallLocked(config_selector);
  // ConfigSelector must be unreffed inside the WorkSerializer.
  if (config_selector.ok()) {
    chand()->work_serializer_->Run(
        [config_selector = std::move(*config_selector)]() mutable {
          config_selector.reset();
        },
        DEBUG_LOCATION);
  }
  // Handle errors.
  if (!error.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: error applying config to call: error=%s",
              chand(), this, StatusToString(error).c_str());
    }
    return error;
  }
  // If the call was queued, add trace annotation.
  if (was_queued) {
    auto* call_tracer = static_cast<CallTracerAnnotationInterface*>(
        call_context()[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value);
    if (call_tracer != nullptr) {
      call_tracer->RecordAnnotation("Delayed name resolution complete.");
    }
  }
  return absl::OkStatus();
}

bool ClientChannel::CallData::CheckResolutionLocked(
    absl::StatusOr<RefCountedPtr<ConfigSelector>>* config_selector) {
  // If we don't yet have a resolver result, we need to queue the call
  // until we get one.
  if (GPR_UNLIKELY(!chand()->received_service_config_data_)) {
    // If the resolver returned transient failure before returning the
    // first service config, fail any non-wait_for_ready calls.
    absl::Status resolver_error = chand()->resolver_transient_failure_error_;
    if (!resolver_error.ok() &&
        !send_initial_metadata()->GetOrCreatePointer(WaitForReady())->value) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: resolution failed, failing call",
                chand(), this);
      }
      *config_selector = absl_status_to_grpc_error(resolver_error);
      return true;
    }
    // Either the resolver has not yet returned a result, or it has
    // returned transient failure but the call is wait_for_ready.  In
    // either case, queue the call.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: no resolver result yet", chand(),
              this);
    }
    return false;
  }
  // Result found.
  *config_selector = chand()->config_selector_;
  dynamic_filters_ = chand()->dynamic_filters_;
  return true;
}

//
// FilterBasedCallData implementation
//

ClientChannel::FilterBasedCallData::FilterBasedCallData(
    grpc_call_element* elem, const grpc_call_element_args& args)
    : path_(CSliceRef(args.path)),
      call_context_(args.context),
      call_start_time_(args.start_time),
      deadline_(args.deadline),
      deadline_state_(elem, args,
                      GPR_LIKELY(static_cast<ClientChannel*>(elem->channel_data)
                                     ->deadline_checking_enabled_)
                          ? args.deadline
                          : Timestamp::InfFuture()) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: created call", chand(), this);
  }
}

ClientChannel::FilterBasedCallData::~FilterBasedCallData() {
  CSliceUnref(path_);
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i] == nullptr);
  }
}

grpc_error_handle ClientChannel::FilterBasedCallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  new (elem->call_data) FilterBasedCallData(elem, *args);
  return absl::OkStatus();
}

void ClientChannel::FilterBasedCallData::Destroy(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* then_schedule_closure) {
  auto* calld = static_cast<FilterBasedCallData*>(elem->call_data);
  RefCountedPtr<DynamicFilters::Call> dynamic_call =
      std::move(calld->dynamic_call_);
  calld->~FilterBasedCallData();
  if (GPR_LIKELY(dynamic_call != nullptr)) {
    dynamic_call->SetAfterCallStackDestroy(then_schedule_closure);
  } else {
    // TODO(yashkt) : This can potentially be a Closure::Run
    ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, absl::OkStatus());
  }
}

void ClientChannel::FilterBasedCallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  auto* calld = static_cast<FilterBasedCallData*>(elem->call_data);
  ClientChannel* chand = static_cast<ClientChannel*>(elem->channel_data);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace) &&
      !GRPC_TRACE_FLAG_ENABLED(grpc_trace_channel)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: batch started from above: %s", chand,
            calld, grpc_transport_stream_op_batch_string(batch, false).c_str());
  }
  if (GPR_LIKELY(chand->deadline_checking_enabled_)) {
    grpc_deadline_state_client_start_transport_stream_op_batch(
        &calld->deadline_state_, batch);
  }
  // Intercept recv_trailing_metadata to commit the call, in case we wind up
  // failing the call before we get down to the retry or LB call layer.
  if (batch->recv_trailing_metadata) {
    calld->original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    GRPC_CLOSURE_INIT(&calld->recv_trailing_metadata_ready_,
                      RecvTrailingMetadataReadyForConfigSelectorCommitCallback,
                      calld, nullptr);
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready_;
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
  if (GPR_UNLIKELY(!calld->cancel_error_.ok())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: failing batch with error: %s",
              chand, calld, StatusToString(calld->cancel_error_).c_str());
    }
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, calld->cancel_error_, calld->call_combiner());
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    calld->cancel_error_ = batch->payload->cancel_stream.cancel_error;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p calld=%p: recording cancel_error=%s", chand,
              calld, StatusToString(calld->cancel_error_).c_str());
    }
    // Fail all pending batches.
    calld->PendingBatchesFail(calld->cancel_error_, NoYieldCallCombiner);
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(
        batch, calld->cancel_error_, calld->call_combiner());
    return;
  }
  // Add the batch to the pending list.
  calld->PendingBatchesAdd(batch);
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
    // If we're still in IDLE, we need to start resolving.
    if (GPR_UNLIKELY(chand->CheckConnectivityState(false) ==
                     GRPC_CHANNEL_IDLE)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO, "chand=%p calld=%p: triggering exit idle", chand,
                calld);
      }
      // Bounce into the control plane work serializer to start resolving.
      GRPC_CHANNEL_STACK_REF(chand->owning_stack_, "ExitIdle");
      chand->work_serializer_->Run(
          [chand]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*chand->work_serializer_) {
            chand->CheckConnectivityState(/*try_to_connect=*/true);
            GRPC_CHANNEL_STACK_UNREF(chand->owning_stack_, "ExitIdle");
          },
          DEBUG_LOCATION);
    }
    calld->TryCheckResolution(/*was_queued=*/false);
  } else {
    // For all other batches, release the call combiner.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: saved batch, yielding call combiner", chand,
              calld);
    }
    GRPC_CALL_COMBINER_STOP(calld->call_combiner(),
                            "batch does not include send_initial_metadata");
  }
}

void ClientChannel::FilterBasedCallData::SetPollent(
    grpc_call_element* elem, grpc_polling_entity* pollent) {
  auto* calld = static_cast<FilterBasedCallData*>(elem->call_data);
  calld->pollent_ = pollent;
}

size_t ClientChannel::FilterBasedCallData::GetBatchIndex(
    grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in CheckResolution() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedCallData::PendingBatchesAdd(
    grpc_transport_stream_op_batch* batch) {
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: adding pending batch at index %" PRIuPTR,
            chand(), this, idx);
  }
  grpc_transport_stream_op_batch*& pending = pending_batches_[idx];
  GPR_ASSERT(pending == nullptr);
  pending = batch;
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedCallData::FailPendingBatchInCallCombiner(
    void* arg, grpc_error_handle error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* calld =
      static_cast<FilterBasedCallData*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(batch, error,
                                                     calld->call_combiner());
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedCallData::PendingBatchesFail(
    grpc_error_handle error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GPR_ASSERT(!error.ok());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: failing %" PRIuPTR " pending batches: %s",
            chand(), this, num_batches, StatusToString(error).c_str());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        FailPendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, error,
                   "PendingBatchesFail");
      batch = nullptr;
    }
  }
  if (yield_call_combiner_predicate(closures)) {
    closures.RunClosures(call_combiner());
  } else {
    closures.RunClosuresWithoutYielding(call_combiner());
  }
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedCallData::ResumePendingBatchInCallCombiner(
    void* arg, grpc_error_handle /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* calld =
      static_cast<FilterBasedCallData*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  calld->dynamic_call_->StartTransportStreamOpBatch(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedCallData::PendingBatchesResume() {
  // Retries not enabled; send down batches as-is.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: starting %" PRIuPTR
            " pending batches on dynamic_call=%p",
            chand(), this, num_batches, dynamic_call_.get());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        ResumePendingBatchInCallCombiner, batch, nullptr);
      closures.Add(&batch->handler_private.closure, absl::OkStatus(),
                   "resuming pending batch from client channel call");
      batch = nullptr;
    }
  }
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner());
}

// A class to handle the call combiner cancellation callback for a
// queued pick.
class ClientChannel::FilterBasedCallData::ResolverQueuedCallCanceller {
 public:
  explicit ResolverQueuedCallCanceller(FilterBasedCallData* calld)
      : calld_(calld) {
    GRPC_CALL_STACK_REF(calld->owning_call(), "ResolverQueuedCallCanceller");
    GRPC_CLOSURE_INIT(&closure_, &CancelLocked, this,
                      grpc_schedule_on_exec_ctx);
    calld->call_combiner()->SetNotifyOnCancel(&closure_);
  }

 private:
  static void CancelLocked(void* arg, grpc_error_handle error) {
    auto* self = static_cast<ResolverQueuedCallCanceller*>(arg);
    auto* calld = self->calld_;
    auto* chand = calld->chand();
    {
      MutexLock lock(&chand->resolution_mu_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: cancelling resolver queued pick: "
                "error=%s self=%p calld->resolver_pick_canceller=%p",
                chand, calld, StatusToString(error).c_str(), self,
                calld->resolver_call_canceller_);
      }
      if (calld->resolver_call_canceller_ == self && !error.ok()) {
        // Remove pick from list of queued picks.
        calld->RemoveCallFromResolverQueuedCallsLocked();
        chand->resolver_queued_calls_.erase(calld);
        // Fail pending batches on the call.
        calld->PendingBatchesFail(error,
                                  YieldCallCombinerIfPendingBatchesFound);
      }
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call(), "ResolvingQueuedCallCanceller");
    delete self;
  }

  FilterBasedCallData* calld_;
  grpc_closure closure_;
};

void ClientChannel::FilterBasedCallData::TryCheckResolution(bool was_queued) {
  auto result = CheckResolution(was_queued);
  if (result.has_value()) {
    if (!result->ok()) {
      PendingBatchesFail(*result, YieldCallCombiner);
      return;
    }
    CreateDynamicCall();
  }
}

void ClientChannel::FilterBasedCallData::OnAddToQueueLocked() {
  // Register call combiner cancellation callback.
  resolver_call_canceller_ = new ResolverQueuedCallCanceller(this);
}

void ClientChannel::FilterBasedCallData::RetryCheckResolutionLocked() {
  // Lame the call combiner canceller.
  resolver_call_canceller_ = nullptr;
  // Do an async callback to resume call processing, so that we're not
  // doing it while holding the channel's resolution mutex.
  chand()->owning_stack_->EventEngine()->Run([this]() {
    ApplicationCallbackExecCtx application_exec_ctx;
    ExecCtx exec_ctx;
    TryCheckResolution(/*was_queued=*/true);
  });
}

void ClientChannel::FilterBasedCallData::CreateDynamicCall() {
  DynamicFilters::Call::Args args = {dynamic_filters(), pollent_,       path_,
                                     call_start_time_,  deadline_,      arena(),
                                     call_context_,     call_combiner()};
  grpc_error_handle error;
  DynamicFilters* channel_stack = args.channel_stack.get();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(
        GPR_INFO,
        "chand=%p calld=%p: creating dynamic call stack on channel_stack=%p",
        chand(), this, channel_stack);
  }
  dynamic_call_ = channel_stack->CreateCall(std::move(args), &error);
  if (!error.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: failed to create dynamic call: error=%s",
              chand(), this, StatusToString(error).c_str());
    }
    PendingBatchesFail(error, YieldCallCombiner);
    return;
  }
  PendingBatchesResume();
}

void ClientChannel::FilterBasedCallData::
    RecvTrailingMetadataReadyForConfigSelectorCommitCallback(
        void* arg, grpc_error_handle error) {
  auto* calld = static_cast<FilterBasedCallData*>(arg);
  auto* chand = calld->chand();
  auto* service_config_call_data =
      GetServiceConfigCallData(calld->call_context());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p calld=%p: got recv_trailing_metadata_ready: error=%s "
            "service_config_call_data=%p",
            chand, calld, StatusToString(error).c_str(),
            service_config_call_data);
  }
  if (service_config_call_data != nullptr) {
    service_config_call_data->Commit();
  }
  // Chain to original callback.
  Closure::Run(DEBUG_LOCATION, calld->original_recv_trailing_metadata_ready_,
               error);
}

//
// ClientChannel::LoadBalancedCall::LbCallState
//

class ClientChannel::LoadBalancedCall::LbCallState
    : public ClientChannelLbCallState {
 public:
  explicit LbCallState(LoadBalancedCall* lb_call) : lb_call_(lb_call) {}

  void* Alloc(size_t size) override { return lb_call_->arena()->Alloc(size); }

  // Internal API to allow first-party LB policies to access per-call
  // attributes set by the ConfigSelector.
  ServiceConfigCallData::CallAttributeInterface* GetCallAttribute(
      UniqueTypeName type) const override;

 private:
  LoadBalancedCall* lb_call_;
};

//
// ClientChannel::LoadBalancedCall::Metadata
//

class ClientChannel::LoadBalancedCall::Metadata
    : public LoadBalancingPolicy::MetadataInterface {
 public:
  explicit Metadata(grpc_metadata_batch* batch) : batch_(batch) {}

  void Add(absl::string_view key, absl::string_view value) override {
    if (batch_ == nullptr) return;
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
    if (batch_ == nullptr) return {};
    Encoder encoder;
    batch_->Encode(&encoder);
    return encoder.Take();
  }

  absl::optional<absl::string_view> Lookup(absl::string_view key,
                                           std::string* buffer) const override {
    if (batch_ == nullptr) return absl::nullopt;
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

    void Encode(GrpcTimeoutMetadata,
                const typename GrpcTimeoutMetadata::ValueType&) {}
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

ServiceConfigCallData::CallAttributeInterface*
ClientChannel::LoadBalancedCall::LbCallState::GetCallAttribute(
    UniqueTypeName type) const {
  auto* service_config_call_data =
      GetServiceConfigCallData(lb_call_->call_context());
  return service_config_call_data->GetCallAttribute(type);
}

//
// ClientChannel::LoadBalancedCall::BackendMetricAccessor
//

class ClientChannel::LoadBalancedCall::BackendMetricAccessor
    : public LoadBalancingPolicy::BackendMetricAccessor {
 public:
  BackendMetricAccessor(LoadBalancedCall* lb_call,
                        grpc_metadata_batch* recv_trailing_metadata)
      : lb_call_(lb_call), recv_trailing_metadata_(recv_trailing_metadata) {}

  const BackendMetricData* GetBackendMetricData() override {
    if (lb_call_->backend_metric_data_ == nullptr &&
        recv_trailing_metadata_ != nullptr) {
      if (const auto* md = recv_trailing_metadata_->get_pointer(
              EndpointLoadMetricsBinMetadata())) {
        BackendMetricAllocator allocator(lb_call_->arena());
        lb_call_->backend_metric_data_ =
            ParseBackendMetricData(md->as_string_view(), &allocator);
      }
    }
    return lb_call_->backend_metric_data_;
  }

 private:
  class BackendMetricAllocator : public BackendMetricAllocatorInterface {
   public:
    explicit BackendMetricAllocator(Arena* arena) : arena_(arena) {}

    BackendMetricData* AllocateBackendMetricData() override {
      return arena_->New<BackendMetricData>();
    }

    char* AllocateString(size_t size) override {
      return static_cast<char*>(arena_->Alloc(size));
    }

   private:
    Arena* arena_;
  };

  LoadBalancedCall* lb_call_;
  grpc_metadata_batch* recv_trailing_metadata_;
};

//
// ClientChannel::LoadBalancedCall
//

namespace {

ClientCallTracer::CallAttemptTracer* CreateCallAttemptTracer(
    grpc_call_context_element* context, bool is_transparent_retry) {
  auto* call_tracer = static_cast<ClientCallTracer*>(
      context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value);
  if (call_tracer == nullptr) return nullptr;
  auto* tracer = call_tracer->StartNewAttempt(is_transparent_retry);
  context[GRPC_CONTEXT_CALL_TRACER].value = tracer;
  return tracer;
}

}  // namespace

ClientChannel::LoadBalancedCall::LoadBalancedCall(
    ClientChannel* chand, grpc_call_context_element* call_context,
    absl::AnyInvocable<void()> on_commit, bool is_transparent_retry)
    : InternallyRefCounted(
          GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)
              ? "LoadBalancedCall"
              : nullptr),
      chand_(chand),
      on_commit_(std::move(on_commit)) {
  CreateCallAttemptTracer(call_context, is_transparent_retry);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p lb_call=%p: created", chand_, this);
  }
}

ClientChannel::LoadBalancedCall::~LoadBalancedCall() {
  if (backend_metric_data_ != nullptr) {
    backend_metric_data_->BackendMetricData::~BackendMetricData();
  }
}

void ClientChannel::LoadBalancedCall::RecordCallCompletion(
    absl::Status status, grpc_metadata_batch* recv_trailing_metadata,
    grpc_transport_stream_stats* transport_stream_stats,
    absl::string_view peer_address) {
  // If we have a tracer, notify it.
  if (call_attempt_tracer() != nullptr) {
    call_attempt_tracer()->RecordReceivedTrailingMetadata(
        status, recv_trailing_metadata, transport_stream_stats);
  }
  // If the LB policy requested a callback for trailing metadata, invoke
  // the callback.
  if (lb_subchannel_call_tracker_ != nullptr) {
    Metadata trailing_metadata(recv_trailing_metadata);
    BackendMetricAccessor backend_metric_accessor(this, recv_trailing_metadata);
    LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
        peer_address, status, &trailing_metadata, &backend_metric_accessor};
    lb_subchannel_call_tracker_->Finish(args);
    lb_subchannel_call_tracker_.reset();
  }
}

void ClientChannel::LoadBalancedCall::RecordLatency() {
  // Compute latency and report it to the tracer.
  if (call_attempt_tracer() != nullptr) {
    gpr_timespec latency =
        gpr_cycle_counter_sub(gpr_get_cycle_counter(), lb_call_start_time_);
    call_attempt_tracer()->RecordEnd(latency);
  }
}

void ClientChannel::LoadBalancedCall::RemoveCallFromLbQueuedCallsLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p lb_call=%p: removing from queued picks list",
            chand_, this);
  }
  // Remove pollset_set linkage.
  grpc_polling_entity_del_from_pollset_set(pollent(),
                                           chand_->interested_parties_);
  // Note: There's no need to actually remove the call from the queue
  // here, beacuse that will be done in either
  // LbQueuedCallCanceller::CancelLocked() or
  // in ClientChannel::UpdateStateAndPickerLocked().
}

void ClientChannel::LoadBalancedCall::AddCallToLbQueuedCallsLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p lb_call=%p: adding to queued picks list",
            chand_, this);
  }
  // Add call's pollent to channel's interested_parties, so that I/O
  // can be done under the call's CQ.
  grpc_polling_entity_add_to_pollset_set(pollent(),
                                         chand_->interested_parties_);
  // Add to queue.
  chand_->lb_queued_calls_.insert(Ref());
  OnAddToQueueLocked();
}

absl::optional<absl::Status> ClientChannel::LoadBalancedCall::PickSubchannel(
    bool was_queued) {
  // We may accumulate multiple pickers here, because if a picker says
  // to queue the call, we check again to see if the picker has been
  // updated before we queue it.
  // We need to unref pickers in the WorkSerializer.
  std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers;
  auto cleanup = absl::MakeCleanup([&]() {
    if (IsClientChannelSubchannelWrapperWorkSerializerOrphanEnabled()) {
      return;
    }
    chand_->work_serializer_->Run(
        [pickers = std::move(pickers)]() mutable {
          for (auto& picker : pickers) {
            picker.reset(DEBUG_LOCATION, "PickSubchannel");
          }
        },
        DEBUG_LOCATION);
  });
  absl::AnyInvocable<void(RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>)>
      set_picker;
  if (!IsClientChannelSubchannelWrapperWorkSerializerOrphanEnabled()) {
    set_picker =
        [&](RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) {
          pickers.emplace_back(std::move(picker));
        };

  } else {
    pickers.emplace_back();
    set_picker =
        [&](RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) {
          pickers[0] = std::move(picker);
        };
  }
  // Grab mutex and take a ref to the picker.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p lb_call=%p: grabbing LB mutex to get picker",
            chand_, this);
  }
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker;
  {
    MutexLock lock(&chand_->lb_mu_);
    set_picker(chand_->picker_);
  }
  while (true) {
    // Do pick.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p lb_call=%p: performing pick with picker=%p",
              chand_, this, pickers.back().get());
    }
    grpc_error_handle error;
    bool pick_complete = PickSubchannelImpl(pickers.back().get(), &error);
    if (!pick_complete) {
      MutexLock lock(&chand_->lb_mu_);
      // If picker has been swapped out since we grabbed it, try again.
      if (pickers.back() != chand_->picker_) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO,
                  "chand=%p lb_call=%p: pick not complete, but picker changed",
                  chand_, this);
        }
        set_picker(chand_->picker_);
        continue;
      }
      // Otherwise queue the pick to try again later when we get a new picker.
      AddCallToLbQueuedCallsLocked();
      return absl::nullopt;
    }
    // Pick is complete.
    // If it was queued, add a trace annotation.
    if (was_queued && call_attempt_tracer() != nullptr) {
      call_attempt_tracer()->RecordAnnotation("Delayed LB pick complete.");
    }
    // If the pick failed, fail the call.
    if (!error.ok()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p lb_call=%p: failed to pick subchannel: error=%s",
                chand_, this, StatusToString(error).c_str());
      }
      return error;
    }
    // Pick succeeded.
    Commit();
    return absl::OkStatus();
  }
}

bool ClientChannel::LoadBalancedCall::PickSubchannelImpl(
    LoadBalancingPolicy::SubchannelPicker* picker, grpc_error_handle* error) {
  GPR_ASSERT(connected_subchannel_ == nullptr);
  // Perform LB pick.
  LoadBalancingPolicy::PickArgs pick_args;
  Slice* path = send_initial_metadata()->get_pointer(HttpPathMetadata());
  GPR_ASSERT(path != nullptr);
  pick_args.path = path->as_string_view();
  LbCallState lb_call_state(this);
  pick_args.call_state = &lb_call_state;
  Metadata initial_metadata(send_initial_metadata());
  pick_args.initial_metadata = &initial_metadata;
  auto result = picker->Pick(pick_args);
  return HandlePickResult<bool>(
      &result,
      // CompletePick
      [this](LoadBalancingPolicy::PickResult::Complete* complete_pick) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO,
                  "chand=%p lb_call=%p: LB pick succeeded: subchannel=%p",
                  chand_, this, complete_pick->subchannel.get());
        }
        GPR_ASSERT(complete_pick->subchannel != nullptr);
        // Grab a ref to the connected subchannel while we're still
        // holding the data plane mutex.
        SubchannelWrapper* subchannel =
            static_cast<SubchannelWrapper*>(complete_pick->subchannel.get());
        connected_subchannel_ = subchannel->connected_subchannel();
        // If the subchannel has no connected subchannel (e.g., if the
        // subchannel has moved out of state READY but the LB policy hasn't
        // yet seen that change and given us a new picker), then just
        // queue the pick.  We'll try again as soon as we get a new picker.
        if (connected_subchannel_ == nullptr) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
            gpr_log(GPR_INFO,
                    "chand=%p lb_call=%p: subchannel returned by LB picker "
                    "has no connected subchannel; queueing pick",
                    chand_, this);
          }
          return false;
        }
        lb_subchannel_call_tracker_ =
            std::move(complete_pick->subchannel_call_tracker);
        if (lb_subchannel_call_tracker_ != nullptr) {
          lb_subchannel_call_tracker_->Start();
        }
        return true;
      },
      // QueuePick
      [this](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO, "chand=%p lb_call=%p: LB pick queued", chand_,
                  this);
        }
        return false;
      },
      // FailPick
      [this, &error](LoadBalancingPolicy::PickResult::Fail* fail_pick) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO, "chand=%p lb_call=%p: LB pick failed: %s", chand_,
                  this, fail_pick->status.ToString().c_str());
        }
        // If wait_for_ready is false, then the error indicates the RPC
        // attempt's final status.
        if (!send_initial_metadata()
                 ->GetOrCreatePointer(WaitForReady())
                 ->value) {
          *error = absl_status_to_grpc_error(MaybeRewriteIllegalStatusCode(
              std::move(fail_pick->status), "LB pick"));
          return true;
        }
        // If wait_for_ready is true, then queue to retry when we get a new
        // picker.
        return false;
      },
      // DropPick
      [this, &error](LoadBalancingPolicy::PickResult::Drop* drop_pick) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO, "chand=%p lb_call=%p: LB pick dropped: %s", chand_,
                  this, drop_pick->status.ToString().c_str());
        }
        *error = grpc_error_set_int(
            absl_status_to_grpc_error(MaybeRewriteIllegalStatusCode(
                std::move(drop_pick->status), "LB drop")),
            StatusIntProperty::kLbPolicyDrop, 1);
        return true;
      });
}

//
// ClientChannel::FilterBasedLoadBalancedCall
//

ClientChannel::FilterBasedLoadBalancedCall::FilterBasedLoadBalancedCall(
    ClientChannel* chand, const grpc_call_element_args& args,
    grpc_polling_entity* pollent, grpc_closure* on_call_destruction_complete,
    absl::AnyInvocable<void()> on_commit, bool is_transparent_retry)
    : LoadBalancedCall(chand, args.context, std::move(on_commit),
                       is_transparent_retry),
      deadline_(args.deadline),
      arena_(args.arena),
      call_context_(args.context),
      owning_call_(args.call_stack),
      call_combiner_(args.call_combiner),
      pollent_(pollent),
      on_call_destruction_complete_(on_call_destruction_complete) {}

ClientChannel::FilterBasedLoadBalancedCall::~FilterBasedLoadBalancedCall() {
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GPR_ASSERT(pending_batches_[i] == nullptr);
  }
  if (on_call_destruction_complete_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_call_destruction_complete_,
                 absl::OkStatus());
  }
}

void ClientChannel::FilterBasedLoadBalancedCall::Orphan() {
  // If the recv_trailing_metadata op was never started, then notify
  // about call completion here, as best we can.  We assume status
  // CANCELLED in this case.
  if (recv_trailing_metadata_ == nullptr) {
    RecordCallCompletion(absl::CancelledError("call cancelled"), nullptr,
                         nullptr, "");
  }
  RecordLatency();
  // Delegate to parent.
  LoadBalancedCall::Orphan();
}

size_t ClientChannel::FilterBasedLoadBalancedCall::GetBatchIndex(
    grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in PickSubchannelImpl() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedLoadBalancedCall::PendingBatchesAdd(
    grpc_transport_stream_op_batch* batch) {
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: adding pending batch at index %" PRIuPTR,
            chand(), this, idx);
  }
  GPR_ASSERT(pending_batches_[idx] == nullptr);
  pending_batches_[idx] = batch;
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedLoadBalancedCall::FailPendingBatchInCallCombiner(
    void* arg, grpc_error_handle error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* self = static_cast<FilterBasedLoadBalancedCall*>(
      batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(batch, error,
                                                     self->call_combiner_);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedLoadBalancedCall::PendingBatchesFail(
    grpc_error_handle error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GPR_ASSERT(!error.ok());
  failure_error_ = error;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: failing %" PRIuPTR " pending batches: %s",
            chand(), this, num_batches, StatusToString(error).c_str());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        FailPendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, error,
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
void ClientChannel::FilterBasedLoadBalancedCall::
    ResumePendingBatchInCallCombiner(void* arg, grpc_error_handle /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  SubchannelCall* subchannel_call =
      static_cast<SubchannelCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  subchannel_call->StartTransportStreamOpBatch(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
void ClientChannel::FilterBasedLoadBalancedCall::PendingBatchesResume() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: starting %" PRIuPTR
            " pending batches on subchannel_call=%p",
            chand(), this, num_batches, subchannel_call_.get());
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = subchannel_call_.get();
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        ResumePendingBatchInCallCombiner, batch,
                        grpc_schedule_on_exec_ctx);
      closures.Add(&batch->handler_private.closure, absl::OkStatus(),
                   "resuming pending batch from LB call");
      batch = nullptr;
    }
  }
  // Note: This will release the call combiner.
  closures.RunClosures(call_combiner_);
}

void ClientChannel::FilterBasedLoadBalancedCall::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace) ||
      GRPC_TRACE_FLAG_ENABLED(grpc_trace_channel)) {
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: batch started from above: %s, "
            "call_attempt_tracer()=%p",
            chand(), this,
            grpc_transport_stream_op_batch_string(batch, false).c_str(),
            call_attempt_tracer());
  }
  // Handle call tracing.
  if (call_attempt_tracer() != nullptr) {
    // Record send ops in tracer.
    if (batch->cancel_stream) {
      call_attempt_tracer()->RecordCancel(
          batch->payload->cancel_stream.cancel_error);
    }
    if (batch->send_initial_metadata) {
      call_attempt_tracer()->RecordSendInitialMetadata(
          batch->payload->send_initial_metadata.send_initial_metadata);
    }
    if (batch->send_trailing_metadata) {
      call_attempt_tracer()->RecordSendTrailingMetadata(
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p lb_call=%p: starting batch on subchannel_call=%p",
              chand(), this, subchannel_call_.get());
    }
    subchannel_call_->StartTransportStreamOpBatch(batch);
    return;
  }
  // We do not yet have a subchannel call.
  //
  // If we've previously been cancelled, immediately fail any new batches.
  if (GPR_UNLIKELY(!cancel_error_.ok())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p lb_call=%p: failing batch with error: %s",
              chand(), this, StatusToString(cancel_error_).c_str());
    }
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(batch, cancel_error_,
                                                       call_combiner_);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    cancel_error_ = batch->payload->cancel_stream.cancel_error;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
      gpr_log(GPR_INFO, "chand=%p lb_call=%p: recording cancel_error=%s",
              chand(), this, StatusToString(cancel_error_).c_str());
    }
    // Fail all pending batches.
    PendingBatchesFail(cancel_error_, NoYieldCallCombiner);
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(batch, cancel_error_,
                                                       call_combiner_);
    return;
  }
  // Add the batch to the pending list.
  PendingBatchesAdd(batch);
  // For batches containing a send_initial_metadata op, acquire the
  // channel's LB mutex to pick a subchannel.
  if (GPR_LIKELY(batch->send_initial_metadata)) {
    TryPick(/*was_queued=*/false);
  } else {
    // For all other batches, release the call combiner.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p lb_call=%p: saved batch, yielding call combiner",
              chand(), this);
    }
    GRPC_CALL_COMBINER_STOP(call_combiner_,
                            "batch does not include send_initial_metadata");
  }
}

void ClientChannel::FilterBasedLoadBalancedCall::RecvInitialMetadataReady(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<FilterBasedLoadBalancedCall*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: got recv_initial_metadata_ready: error=%s",
            self->chand(), self, StatusToString(error).c_str());
  }
  if (error.ok()) {
    // recv_initial_metadata_flags is not populated for clients
    self->call_attempt_tracer()->RecordReceivedInitialMetadata(
        self->recv_initial_metadata_);
    auto* peer_string = self->recv_initial_metadata_->get_pointer(PeerString());
    if (peer_string != nullptr) self->peer_string_ = peer_string->Ref();
  }
  Closure::Run(DEBUG_LOCATION, self->original_recv_initial_metadata_ready_,
               error);
}

void ClientChannel::FilterBasedLoadBalancedCall::RecvTrailingMetadataReady(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<FilterBasedLoadBalancedCall*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: got recv_trailing_metadata_ready: error=%s "
            "call_attempt_tracer()=%p lb_subchannel_call_tracker_=%p "
            "failure_error_=%s",
            self->chand(), self, StatusToString(error).c_str(),
            self->call_attempt_tracer(), self->lb_subchannel_call_tracker(),
            StatusToString(self->failure_error_).c_str());
  }
  // Check if we have a tracer or an LB callback to invoke.
  if (self->call_attempt_tracer() != nullptr ||
      self->lb_subchannel_call_tracker() != nullptr) {
    // Get the call's status.
    absl::Status status;
    if (!error.ok()) {
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
    absl::string_view peer_string;
    if (self->peer_string_.has_value()) {
      peer_string = self->peer_string_->as_string_view();
    }
    self->RecordCallCompletion(status, self->recv_trailing_metadata_,
                               self->transport_stream_stats_, peer_string);
  }
  // Chain to original callback.
  if (!self->failure_error_.ok()) {
    error = self->failure_error_;
    self->failure_error_ = absl::OkStatus();
  }
  Closure::Run(DEBUG_LOCATION, self->original_recv_trailing_metadata_ready_,
               error);
}

// A class to handle the call combiner cancellation callback for a
// queued pick.
// TODO(roth): When we implement hedging support, we won't be able to
// register a call combiner cancellation closure for each LB pick,
// because there may be multiple LB picks happening in parallel.
// Instead, we will probably need to maintain a list in the CallData
// object of pending LB picks to be cancelled when the closure runs.
class ClientChannel::FilterBasedLoadBalancedCall::LbQueuedCallCanceller {
 public:
  explicit LbQueuedCallCanceller(
      RefCountedPtr<FilterBasedLoadBalancedCall> lb_call)
      : lb_call_(std::move(lb_call)) {
    GRPC_CALL_STACK_REF(lb_call_->owning_call_, "LbQueuedCallCanceller");
    GRPC_CLOSURE_INIT(&closure_, &CancelLocked, this, nullptr);
    lb_call_->call_combiner_->SetNotifyOnCancel(&closure_);
  }

 private:
  static void CancelLocked(void* arg, grpc_error_handle error) {
    auto* self = static_cast<LbQueuedCallCanceller*>(arg);
    auto* lb_call = self->lb_call_.get();
    auto* chand = lb_call->chand();
    {
      MutexLock lock(&chand->lb_mu_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p lb_call=%p: cancelling queued pick: "
                "error=%s self=%p calld->pick_canceller=%p",
                chand, lb_call, StatusToString(error).c_str(), self,
                lb_call->lb_call_canceller_);
      }
      if (lb_call->lb_call_canceller_ == self && !error.ok()) {
        lb_call->Commit();
        // Remove pick from list of queued picks.
        lb_call->RemoveCallFromLbQueuedCallsLocked();
        // Remove from queued picks list.
        chand->lb_queued_calls_.erase(self->lb_call_);
        // Fail pending batches on the call.
        lb_call->PendingBatchesFail(error,
                                    YieldCallCombinerIfPendingBatchesFound);
      }
    }
    // Unref lb_call before unreffing the call stack, since unreffing
    // the call stack may destroy the arena in which lb_call is allocated.
    auto* owning_call = lb_call->owning_call_;
    self->lb_call_.reset();
    GRPC_CALL_STACK_UNREF(owning_call, "LbQueuedCallCanceller");
    delete self;
  }

  RefCountedPtr<FilterBasedLoadBalancedCall> lb_call_;
  grpc_closure closure_;
};

void ClientChannel::FilterBasedLoadBalancedCall::TryPick(bool was_queued) {
  auto result = PickSubchannel(was_queued);
  if (result.has_value()) {
    if (!result->ok()) {
      PendingBatchesFail(*result, YieldCallCombiner);
      return;
    }
    CreateSubchannelCall();
  }
}

void ClientChannel::FilterBasedLoadBalancedCall::OnAddToQueueLocked() {
  // Register call combiner cancellation callback.
  lb_call_canceller_ = new LbQueuedCallCanceller(Ref());
}

void ClientChannel::FilterBasedLoadBalancedCall::RetryPickLocked() {
  // Lame the call combiner canceller.
  lb_call_canceller_ = nullptr;
  // Do an async callback to resume call processing, so that we're not
  // doing it while holding the channel's LB mutex.
  // TODO(roth): We should really be using EventEngine::Run() here
  // instead of ExecCtx::Run().  Unfortunately, doing that seems to cause
  // a flaky TSAN failure for reasons that I do not fully understand.
  // However, given that we are working toward eliminating this code as
  // part of the promise conversion, it doesn't seem worth further
  // investigation right now.
  ExecCtx::Run(DEBUG_LOCATION, NewClosure([this](grpc_error_handle) {
                 // If there are a lot of queued calls here, resuming them
                 // all may cause us to stay inside C-core for a long period
                 // of time. All of that work would be done using the same
                 // ExecCtx instance and therefore the same cached value of
                 // "now". The longer it takes to finish all of this work
                 // and exit from C-core, the more stale the cached value of
                 // "now" may become. This can cause problems whereby (e.g.)
                 // we calculate a timer deadline based on the stale value,
                 // which results in the timer firing too early. To avoid
                 // this, we invalidate the cached value for each call we
                 // process.
                 ExecCtx::Get()->InvalidateNow();
                 TryPick(/*was_queued=*/true);
               }),
               absl::OkStatus());
}

void ClientChannel::FilterBasedLoadBalancedCall::CreateSubchannelCall() {
  Slice* path = send_initial_metadata()->get_pointer(HttpPathMetadata());
  GPR_ASSERT(path != nullptr);
  SubchannelCall::Args call_args = {
      connected_subchannel()->Ref(), pollent_, path->Ref(), /*start_time=*/0,
      deadline_, arena_,
      // TODO(roth): When we implement hedging support, we will probably
      // need to use a separate call context for each subchannel call.
      call_context_, call_combiner_};
  grpc_error_handle error;
  subchannel_call_ = SubchannelCall::Create(std::move(call_args), &error);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO,
            "chand=%p lb_call=%p: create subchannel_call=%p: error=%s", chand(),
            this, subchannel_call_.get(), StatusToString(error).c_str());
  }
  if (on_call_destruction_complete_ != nullptr) {
    subchannel_call_->SetAfterCallStackDestroy(on_call_destruction_complete_);
    on_call_destruction_complete_ = nullptr;
  }
  if (GPR_UNLIKELY(!error.ok())) {
    PendingBatchesFail(error, YieldCallCombiner);
  } else {
    PendingBatchesResume();
  }
}

//
// ClientChannel::PromiseBasedLoadBalancedCall
//

ClientChannel::PromiseBasedLoadBalancedCall::PromiseBasedLoadBalancedCall(
    ClientChannel* chand, absl::AnyInvocable<void()> on_commit,
    bool is_transparent_retry)
    : LoadBalancedCall(chand, GetContext<grpc_call_context_element>(),
                       std::move(on_commit), is_transparent_retry) {}

ArenaPromise<ServerMetadataHandle>
ClientChannel::PromiseBasedLoadBalancedCall::MakeCallPromise(
    CallArgs call_args, OrphanablePtr<PromiseBasedLoadBalancedCall> lb_call) {
  pollent_ = NowOrNever(call_args.polling_entity->WaitAndCopy()).value();
  // Record ops in tracer.
  if (call_attempt_tracer() != nullptr) {
    call_attempt_tracer()->RecordSendInitialMetadata(
        call_args.client_initial_metadata.get());
    // TODO(ctiller): Find a way to do this without registering a no-op mapper.
    call_args.client_to_server_messages->InterceptAndMapWithHalfClose(
        [](MessageHandle message) { return message; },  // No-op.
        [this]() {
          // TODO(roth): Change CallTracer API to not pass metadata
          // batch to this method, since the batch is always empty.
          grpc_metadata_batch metadata(GetContext<Arena>());
          call_attempt_tracer()->RecordSendTrailingMetadata(&metadata);
        });
  }
  // Extract peer name from server initial metadata.
  call_args.server_initial_metadata->InterceptAndMap(
      [this](ServerMetadataHandle metadata) {
        if (call_attempt_tracer() != nullptr) {
          call_attempt_tracer()->RecordReceivedInitialMetadata(metadata.get());
        }
        Slice* peer_string = metadata->get_pointer(PeerString());
        if (peer_string != nullptr) peer_string_ = peer_string->Ref();
        return metadata;
      });
  client_initial_metadata_ = std::move(call_args.client_initial_metadata);
  return OnCancel(
      Map(TrySeq(
              // LB pick.
              [this]() -> Poll<absl::Status> {
                auto result = PickSubchannel(was_queued_);
                if (GRPC_TRACE_FLAG_ENABLED(
                        grpc_client_channel_lb_call_trace)) {
                  gpr_log(GPR_INFO,
                          "chand=%p lb_call=%p: %sPickSubchannel() returns %s",
                          chand(), this,
                          Activity::current()->DebugTag().c_str(),
                          result.has_value() ? result->ToString().c_str()
                                             : "Pending");
                }
                if (result == absl::nullopt) return Pending{};
                return std::move(*result);
              },
              [this, call_args = std::move(call_args)]() mutable
              -> ArenaPromise<ServerMetadataHandle> {
                call_args.client_initial_metadata =
                    std::move(client_initial_metadata_);
                return connected_subchannel()->MakeCallPromise(
                    std::move(call_args));
              }),
          // Record call completion.
          [this](ServerMetadataHandle metadata) {
            if (call_attempt_tracer() != nullptr ||
                lb_subchannel_call_tracker() != nullptr) {
              absl::Status status;
              grpc_status_code code = metadata->get(GrpcStatusMetadata())
                                          .value_or(GRPC_STATUS_UNKNOWN);
              if (code != GRPC_STATUS_OK) {
                absl::string_view message;
                if (const auto* grpc_message =
                        metadata->get_pointer(GrpcMessageMetadata())) {
                  message = grpc_message->as_string_view();
                }
                status =
                    absl::Status(static_cast<absl::StatusCode>(code), message);
              }
              RecordCallCompletion(status, metadata.get(),
                                   &GetContext<CallContext>()
                                        ->call_stats()
                                        ->transport_stream_stats,
                                   peer_string_.as_string_view());
            }
            RecordLatency();
            return metadata;
          }),
      [lb_call = std::move(lb_call)]() {
        // If the waker is pending, then we need to remove ourself from
        // the list of queued LB calls.
        if (!lb_call->waker_.is_unwakeable()) {
          MutexLock lock(&lb_call->chand()->lb_mu_);
          lb_call->Commit();
          // Remove pick from list of queued picks.
          lb_call->RemoveCallFromLbQueuedCallsLocked();
          // Remove from queued picks list.
          lb_call->chand()->lb_queued_calls_.erase(lb_call.get());
        }
        // TODO(ctiller): We don't have access to the call's actual status
        // here, so we just assume CANCELLED.  We could change this to use
        // CallFinalization instead of OnCancel() so that we can get the
        // actual status.  But we should also have access to the trailing
        // metadata, which we don't have in either case.  Ultimately, we
        // need a better story for code that needs to run at the end of a
        // call in both cancellation and non-cancellation cases that needs
        // access to server trailing metadata and the call's real status.
        if (lb_call->call_attempt_tracer() != nullptr) {
          lb_call->call_attempt_tracer()->RecordCancel(
              absl::CancelledError("call cancelled"));
        }
        if (lb_call->call_attempt_tracer() != nullptr ||
            lb_call->lb_subchannel_call_tracker() != nullptr) {
          // If we were cancelled without recording call completion, then
          // record call completion here, as best we can.  We assume status
          // CANCELLED in this case.
          lb_call->RecordCallCompletion(absl::CancelledError("call cancelled"),
                                        nullptr, nullptr, "");
        }
      });
}

Arena* ClientChannel::PromiseBasedLoadBalancedCall::arena() const {
  return GetContext<Arena>();
}

grpc_call_context_element*
ClientChannel::PromiseBasedLoadBalancedCall::call_context() const {
  return GetContext<grpc_call_context_element>();
}

grpc_metadata_batch*
ClientChannel::PromiseBasedLoadBalancedCall::send_initial_metadata() const {
  return client_initial_metadata_.get();
}

void ClientChannel::PromiseBasedLoadBalancedCall::OnAddToQueueLocked() {
  waker_ = Activity::current()->MakeNonOwningWaker();
  was_queued_ = true;
}

void ClientChannel::PromiseBasedLoadBalancedCall::RetryPickLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
    gpr_log(GPR_INFO, "chand=%p lb_call=%p: RetryPickLocked()", chand(), this);
  }
  waker_.WakeupAsync();
}

}  // namespace grpc_core
