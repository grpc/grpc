//
// Copyright 2021 gRPC authors.
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

#include "src/core/ext/filters/fault_injection/fault_injection_filter.h"

#include <atomic>

#include "absl/strings/numbers.h"

#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/fault_injection/service_config_parser.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/status_conversion.h"

namespace grpc_core {

TraceFlag grpc_fault_injection_filter_trace(false, "fault_injection_filter");

namespace {

std::atomic<uint32_t> g_active_faults{0};
static_assert(
    std::is_trivially_destructible<std::atomic<uint32_t>>::value,
    "the active fault counter needs to have a trivially destructible type");

template <typename T>
auto AsInt(absl::string_view s) -> absl::optional<T> {
  T x;
  if (absl::SimpleAtoi(s, &x)) return x;
  return absl::nullopt;
}

inline bool UnderFraction(const uint32_t numerator,
                          const uint32_t denominator) {
  if (numerator <= 0) return false;
  if (numerator >= denominator) return true;
  // Generate a random number in [0, denominator).
  const uint32_t random_number = rand() % denominator;
  return random_number < numerator;
}

class ChannelData {
 public:
  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  int index() const { return index_; }

 private:
  ChannelData(grpc_channel_element* elem, grpc_channel_element_args* args);
  ~ChannelData() = default;

  // The relative index of instances of the same filter.
  int index_;
};

class CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);

  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /*final_info*/,
                      grpc_closure* /*then_schedule_closure*/);

  static void StartTransportStreamOpBatch(
      grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

 private:
  class ResumeBatchCanceller;

  CallData(grpc_call_element* elem, const grpc_call_element_args* args);
  ~CallData();

  void DecideWhetherToInjectFaults(grpc_metadata_batch* initial_metadata);

  // Checks if current active faults exceed the allowed max faults.
  bool HaveActiveFaultsQuota(bool increment);

  // Returns true if this RPC needs to be delayed. If so, this call will be
  // counted as an active fault.
  bool MaybeDelay();

  // Returns the aborted RPC status if this RPC needs to be aborted. If so,
  // this call will be counted as an active fault. Otherwise, it returns
  // GRPC_ERROR_NONE.
  // If this call is already been delay injected, skip the active faults
  // quota check.
  grpc_error_handle MaybeAbort();

  // Delays the stream operations batch.
  void DelayBatch(grpc_call_element* elem,
                  grpc_transport_stream_op_batch* batch);

  // Cancels the delay timer.
  void CancelDelayTimer() { grpc_timer_cancel(&delay_timer_); }

  // Finishes the fault injection, should only be called once.
  void FaultInjectionFinished() {
    g_active_faults.fetch_sub(1, std::memory_order_relaxed);
  }

  // This is a callback that will be invoked after the delay timer is up.
  static void ResumeBatch(void* arg, grpc_error_handle error);

  // This is a callback invoked upon completion of recv_trailing_metadata.
  // Injects the abort_error_ to the recv_trailing_metadata batch if needed.
  static void HijackedRecvTrailingMetadataReady(void* arg, grpc_error_handle);

  // Used to track the policy structs that needs to be destroyed in dtor.
  bool fi_policy_owned_ = false;
  const FaultInjectionMethodParsedConfig::FaultInjectionPolicy* fi_policy_;
  grpc_call_stack* owning_call_;
  Arena* arena_;
  CallCombiner* call_combiner_;

  // Indicates whether we are doing a delay and/or an abort for this call.
  bool delay_request_ = false;
  bool abort_request_ = false;

  // Delay states
  grpc_timer delay_timer_ ABSL_GUARDED_BY(delay_mu_);
  ResumeBatchCanceller* resume_batch_canceller_ ABSL_GUARDED_BY(delay_mu_);
  grpc_transport_stream_op_batch* delayed_batch_ ABSL_GUARDED_BY(delay_mu_);
  // Abort states
  grpc_error_handle abort_error_ = GRPC_ERROR_NONE;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ready_;
  // Protects the asynchronous delay, resume, and cancellation.
  Mutex delay_mu_;
};

// ChannelData

grpc_error_handle ChannelData::Init(grpc_channel_element* elem,
                                    grpc_channel_element_args* args) {
  GPR_ASSERT(elem->filter == &FaultInjectionFilterVtable);
  new (elem->channel_data) ChannelData(elem, args);
  return GRPC_ERROR_NONE;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

ChannelData::ChannelData(grpc_channel_element* elem,
                         grpc_channel_element_args* args)
    : index_(grpc_channel_stack_filter_instance_number(args->channel_stack,
                                                       elem)) {}

// CallData::ResumeBatchCanceller

class CallData::ResumeBatchCanceller {
 public:
  explicit ResumeBatchCanceller(grpc_call_element* elem) : elem_(elem) {
    auto* calld = static_cast<CallData*>(elem->call_data);
    GRPC_CALL_STACK_REF(calld->owning_call_, "ResumeBatchCanceller");
    GRPC_CLOSURE_INIT(&closure_, &Cancel, this, grpc_schedule_on_exec_ctx);
    calld->call_combiner_->SetNotifyOnCancel(&closure_);
  }

 private:
  static void Cancel(void* arg, grpc_error_handle error) {
    auto* self = static_cast<ResumeBatchCanceller*>(arg);
    auto* chand = static_cast<ChannelData*>(self->elem_->channel_data);
    auto* calld = static_cast<CallData*>(self->elem_->call_data);
    {
      MutexLock lock(&calld->delay_mu_);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_fault_injection_filter_trace)) {
        gpr_log(GPR_INFO,
                "chand=%p calld=%p: cancelling schdueled pick: "
                "error=%s self=%p calld->resume_batch_canceller_=%p",
                chand, calld, grpc_error_std_string(error).c_str(), self,
                calld->resume_batch_canceller_);
      }
      if (error != GRPC_ERROR_NONE && calld->resume_batch_canceller_ == self) {
        // Cancel the delayed pick.
        calld->CancelDelayTimer();
        calld->FaultInjectionFinished();
        // Fail pending batches on the call.
        grpc_transport_stream_op_batch_finish_with_failure(
            calld->delayed_batch_, GRPC_ERROR_REF(error),
            calld->call_combiner_);
      }
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call_, "ResumeBatchCanceller");
    delete self;
  }

  grpc_call_element* elem_;
  grpc_closure closure_;
};

// CallData

grpc_error_handle CallData::Init(grpc_call_element* elem,
                                 const grpc_call_element_args* args) {
  auto* calld = new (elem->call_data) CallData(elem, args);
  if (calld->fi_policy_ == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "failed to find fault injection policy");
  }
  return GRPC_ERROR_NONE;
}

void CallData::Destroy(grpc_call_element* elem,
                       const grpc_call_final_info* /*final_info*/,
                       grpc_closure* /*then_schedule_closure*/) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

void CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  // There should only be one send_initial_metdata op, and fault injection also
  // only need to be enforced once.
  if (batch->send_initial_metadata) {
    calld->DecideWhetherToInjectFaults(
        batch->payload->send_initial_metadata.send_initial_metadata);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_fault_injection_filter_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: Fault injection triggered delay=%d abort=%d",
              elem->channel_data, calld, calld->delay_request_,
              calld->abort_request_);
    }
    if (calld->MaybeDelay()) {
      // Delay the batch, and pass down the batch in the scheduled closure.
      calld->DelayBatch(elem, batch);
      return;
    }
    grpc_error_handle abort_error = calld->MaybeAbort();
    if (abort_error != GRPC_ERROR_NONE) {
      calld->abort_error_ = abort_error;
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(calld->abort_error_), calld->call_combiner_);
      return;
    }
  } else {
    if (batch->recv_trailing_metadata) {
      // Intercept recv_trailing_metadata callback so that we can inject the
      // failure when aborting streaming calls, because their
      // recv_trailing_metatdata op may not be on the same batch as the
      // send_initial_metadata op.
      calld->original_recv_trailing_metadata_ready_ =
          batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
      batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
          &calld->recv_trailing_metadata_ready_;
    }
    if (calld->abort_error_ != GRPC_ERROR_NONE) {
      // If we already decided to abort, then immediately fail this batch.
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(calld->abort_error_), calld->call_combiner_);
      return;
    }
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, batch);
}

CallData::CallData(grpc_call_element* elem, const grpc_call_element_args* args)
    : owning_call_(args->call_stack),
      arena_(args->arena),
      call_combiner_(args->call_combiner) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  // Fetch the fault injection policy from the service config, based on the
  // relative index for which policy should this CallData use.
  auto* service_config_call_data = static_cast<ServiceConfigCallData*>(
      args->context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  auto* method_params = static_cast<FaultInjectionMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          FaultInjectionServiceConfigParser::ParserIndex()));
  if (method_params != nullptr) {
    fi_policy_ = method_params->fault_injection_policy(chand->index());
  }
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                    HijackedRecvTrailingMetadataReady, elem,
                    grpc_schedule_on_exec_ctx);
}

CallData::~CallData() {
  if (fi_policy_owned_) {
    fi_policy_->~FaultInjectionPolicy();
  }
  GRPC_ERROR_UNREF(abort_error_);
}

void CallData::DecideWhetherToInjectFaults(
    grpc_metadata_batch* initial_metadata) {
  FaultInjectionMethodParsedConfig::FaultInjectionPolicy* copied_policy =
      nullptr;
  // Update the policy with values in initial metadata.
  if (!fi_policy_->abort_code_header.empty() ||
      !fi_policy_->abort_percentage_header.empty() ||
      !fi_policy_->delay_header.empty() ||
      !fi_policy_->delay_percentage_header.empty()) {
    // Defer the actual copy until the first matched header.
    auto maybe_copy_policy_func = [this, &copied_policy]() {
      if (copied_policy == nullptr) {
        copied_policy =
            arena_->New<FaultInjectionMethodParsedConfig::FaultInjectionPolicy>(
                *fi_policy_);
      }
    };
    std::string buffer;
    if (!fi_policy_->abort_code_header.empty() &&
        (copied_policy == nullptr ||
         copied_policy->abort_code == GRPC_STATUS_OK)) {
      auto value = initial_metadata->GetStringValue(
          fi_policy_->abort_code_header, &buffer);
      if (value.has_value()) {
        maybe_copy_policy_func();
        grpc_status_code_from_int(
            AsInt<int>(*value).value_or(GRPC_STATUS_UNKNOWN),
            &copied_policy->abort_code);
      }
    }
    if (!fi_policy_->abort_percentage_header.empty()) {
      auto value = initial_metadata->GetStringValue(
          fi_policy_->abort_percentage_header, &buffer);
      if (value.has_value()) {
        maybe_copy_policy_func();
        copied_policy->abort_percentage_numerator =
            std::min(AsInt<uint32_t>(*value).value_or(-1),
                     fi_policy_->abort_percentage_numerator);
      }
    }
    if (!fi_policy_->delay_header.empty() &&
        (copied_policy == nullptr || copied_policy->delay == 0)) {
      auto value =
          initial_metadata->GetStringValue(fi_policy_->delay_header, &buffer);
      if (value.has_value()) {
        maybe_copy_policy_func();
        copied_policy->delay = static_cast<grpc_millis>(
            std::max(AsInt<int64_t>(*value).value_or(0), int64_t(0)));
      }
    }
    if (!fi_policy_->delay_percentage_header.empty()) {
      auto value = initial_metadata->GetStringValue(
          fi_policy_->delay_percentage_header, &buffer);
      if (value.has_value()) {
        maybe_copy_policy_func();
        copied_policy->delay_percentage_numerator =
            std::min(AsInt<uint32_t>(*value).value_or(-1),
                     fi_policy_->delay_percentage_numerator);
      }
    }
    if (copied_policy != nullptr) fi_policy_ = copied_policy;
  }
  // Roll the dice
  delay_request_ = fi_policy_->delay != 0 &&
                   UnderFraction(fi_policy_->delay_percentage_numerator,
                                 fi_policy_->delay_percentage_denominator);
  abort_request_ = fi_policy_->abort_code != GRPC_STATUS_OK &&
                   UnderFraction(fi_policy_->abort_percentage_numerator,
                                 fi_policy_->abort_percentage_denominator);
  if (!delay_request_ && !abort_request_) {
    if (copied_policy != nullptr) copied_policy->~FaultInjectionPolicy();
    // No fault injection for this call
  } else {
    fi_policy_owned_ = copied_policy != nullptr;
  }
}

bool CallData::HaveActiveFaultsQuota(bool increment) {
  if (g_active_faults.load(std::memory_order_acquire) >=
      fi_policy_->max_faults) {
    return false;
  }
  if (increment) g_active_faults.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool CallData::MaybeDelay() {
  if (delay_request_) {
    return HaveActiveFaultsQuota(true);
  }
  return false;
}

grpc_error_handle CallData::MaybeAbort() {
  if (abort_request_ && (delay_request_ || HaveActiveFaultsQuota(false))) {
    return grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING(fi_policy_->abort_message.c_str()),
        GRPC_ERROR_INT_GRPC_STATUS, fi_policy_->abort_code);
  }
  return GRPC_ERROR_NONE;
}

void CallData::DelayBatch(grpc_call_element* elem,
                          grpc_transport_stream_op_batch* batch) {
  MutexLock lock(&delay_mu_);
  delayed_batch_ = batch;
  resume_batch_canceller_ = new ResumeBatchCanceller(elem);
  grpc_millis resume_time = ExecCtx::Get()->Now() + fi_policy_->delay;
  GRPC_CLOSURE_INIT(&batch->handler_private.closure, ResumeBatch, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&delay_timer_, resume_time, &batch->handler_private.closure);
}

void CallData::ResumeBatch(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  auto* calld = static_cast<CallData*>(elem->call_data);
  MutexLock lock(&calld->delay_mu_);
  // Cancelled or canceller has already run
  if (error == GRPC_ERROR_CANCELLED ||
      calld->resume_batch_canceller_ == nullptr) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_fault_injection_filter_trace)) {
    gpr_log(GPR_INFO, "chand=%p calld=%p: Resuming delayed stream op batch %p",
            elem->channel_data, calld, calld->delayed_batch_);
  }
  // Lame the canceller
  calld->resume_batch_canceller_ = nullptr;
  // Finish fault injection.
  calld->FaultInjectionFinished();
  // Abort if needed.
  error = calld->MaybeAbort();
  if (error != GRPC_ERROR_NONE) {
    calld->abort_error_ = error;
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->delayed_batch_, GRPC_ERROR_REF(calld->abort_error_),
        calld->call_combiner_);
    return;
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, calld->delayed_batch_);
}

void CallData::HijackedRecvTrailingMetadataReady(void* arg,
                                                 grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  auto* calld = static_cast<CallData*>(elem->call_data);
  if (calld->abort_error_ != GRPC_ERROR_NONE) {
    error = grpc_error_add_child(GRPC_ERROR_REF(error),
                                 GRPC_ERROR_REF(calld->abort_error_));
  } else {
    error = GRPC_ERROR_REF(error);
  }
  Closure::Run(DEBUG_LOCATION, calld->original_recv_trailing_metadata_ready_,
               error);
}

}  // namespace

extern const grpc_channel_filter FaultInjectionFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(ChannelData),
    ChannelData::Init,
    ChannelData::Destroy,
    grpc_channel_next_get_info,
    "fault_injection_filter",
};

void FaultInjectionFilterInit(void) {
  FaultInjectionServiceConfigParser::Register();
}

void FaultInjectionFilterShutdown(void) {}

}  // namespace grpc_core
