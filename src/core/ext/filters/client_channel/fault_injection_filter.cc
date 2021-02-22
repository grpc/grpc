//
// Copyright 2020 gRPC authors.
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

#include "src/core/ext/filters/client_channel/fault_injection_filter.h"

#include "absl/strings/numbers.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/service_config_call_data.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/timer.h"

namespace grpc_core {

using internal::ClientChannelMethodParsedConfig;
using internal::ClientChannelServiceConfigParser;

TraceFlag grpc_fault_injection_filter_trace(false, "fault_injection_filter");

namespace {

Atomic<uint32_t> g_active_faults{0};

inline int GetLinkedMetadatumValueInt(grpc_linked_mdelem* md) {
  int res;
  if (absl::SimpleAtoi(StringViewFromSlice(GRPC_MDVALUE(md->md)), &res)) {
    return res;
  } else {
    return -1;
  }
}

inline int64_t GetLinkedMetadatumValueInt64(grpc_linked_mdelem* md) {
  int64_t res;
  if (absl::SimpleAtoi(StringViewFromSlice(GRPC_MDVALUE(md->md)), &res)) {
    return res;
  } else {
    return -1;
  }
}

inline bool UnderFraction(const uint32_t fraction_per_million) {
  if (fraction_per_million == 0) return false;
  // Generate a random number in [0, 1000000).
  const uint32_t random_number = rand() % 1000000;
  return random_number < fraction_per_million;
}

int TranslatePercentageToPerMillion(int percentage, int denominator, int cap) {
  return GPR_MAX(0, GPR_MIN(percentage * (1000000 / denominator), cap));
}

class FaultInjectionFilter {
 public:
  class ChannelData {
   public:
    static grpc_error* Init(grpc_channel_element* elem,
                            grpc_channel_element_args* args);
    static void Destroy(grpc_channel_element* elem);

    int index() const { return index_; }

   private:
    ChannelData() = default;
    ~ChannelData() = default;

    // The relative index of instances of the same filter.
    int index_;
  };

  class CallData {
   public:
    static grpc_error* Init(grpc_call_element* elem,
                            const grpc_call_element_args* args);

    static void Destroy(grpc_call_element* elem,
                        const grpc_call_final_info* /*final_info*/,
                        grpc_closure* /*then_schedule_closure*/);

    static void StartTransportStreamOpBatch(
        grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

   private:
    class ResumeBatchCanceller;

    explicit CallData(const grpc_call_element_args* args);
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
    grpc_error* MaybeAbort();

    // Delays the stream operations batch.
    void DelayBatch(grpc_call_element* elem,
                    grpc_transport_stream_op_batch* batch);

    // Cancels the delay timer.
    void CancelDelayTimer() { grpc_timer_cancel(&delay_timer_); }

    // Finishes the fault injection, should only be called once.
    void FaultInjectionFinished() {
      g_active_faults.FetchSub(1, MemoryOrder::RELAXED);
    }

    static void ResumeBatch(void* arg, grpc_error* error);

    // Used to track the policy structs that needs to be destroyed in dtor.
    bool fi_policy_owned_;
    const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy_;
    grpc_call_stack* owning_call_;
    Arena* arena_;
    CallCombiner* call_combiner_;

    // Indicates whether we are doing a delay and/or an abort for this call.
    bool delay_request_ = false;
    bool abort_request_ = false;

    // Delay states
    grpc_timer delay_timer_;
    ResumeBatchCanceller* resume_batch_canceller_;
    grpc_transport_stream_op_batch* delayed_batch_;
    // Protects the asynchronous delay, resume, and cancellation.
    Mutex delay_mu_;
  };
};

// ChannelData

grpc_error* FaultInjectionFilter::ChannelData::Init(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  GPR_ASSERT(elem->filter == &grpc_fault_injection_filter);
  auto* chand = new (elem->channel_data) FaultInjectionFilter::ChannelData();
  chand->index_ =
      grpc_channel_stack_filter_instance_number(args->channel_stack, elem);
  return GRPC_ERROR_NONE;
}

void FaultInjectionFilter::ChannelData::Destroy(grpc_channel_element* elem) {
  auto* chand =
      static_cast<FaultInjectionFilter::ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

// CallData
class FaultInjectionFilter::CallData::ResumeBatchCanceller {
 public:
  explicit ResumeBatchCanceller(grpc_call_element* elem) : elem_(elem) {
    auto* calld = static_cast<FaultInjectionFilter::CallData*>(elem->call_data);
    GRPC_CALL_STACK_REF(calld->owning_call_, "ResumeBatchCanceller");
    GRPC_CLOSURE_INIT(&closure_, &Cancel, this, grpc_schedule_on_exec_ctx);
    calld->call_combiner_->SetNotifyOnCancel(&closure_);
  }

 private:
  static void Cancel(void* arg, grpc_error* error) {
    auto* self = static_cast<ResumeBatchCanceller*>(arg);
    auto* chand = static_cast<FaultInjectionFilter::ChannelData*>(
        self->elem_->channel_data);
    auto* calld =
        static_cast<FaultInjectionFilter::CallData*>(self->elem_->call_data);
    MutexLock lock(&calld->delay_mu_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_fault_injection_filter_trace)) {
      gpr_log(GPR_INFO,
              "chand=%p calld=%p: cancelling schdueled pick: "
              "error=%s self=%p calld->resume_batch_canceller_=%p",
              chand, calld, grpc_error_string(error), self,
              calld->resume_batch_canceller_);
    }
    if (error != GRPC_ERROR_NONE && calld->resume_batch_canceller_ == self) {
      // Cancel the delayed pick.
      calld->CancelDelayTimer();
      calld->FaultInjectionFinished();
      // Fail pending batches on the call.
      grpc_transport_stream_op_batch_finish_with_failure(
          calld->delayed_batch_, GRPC_ERROR_REF(error), calld->call_combiner_);
    }
    GRPC_CALL_STACK_UNREF(calld->owning_call_, "ResumeBatchCanceller");
    delete self;
  }

  grpc_call_element* elem_;
  grpc_closure closure_;
};

grpc_error* FaultInjectionFilter::CallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  auto* chand =
      static_cast<FaultInjectionFilter::ChannelData*>(elem->channel_data);
  auto* calld = new (elem->call_data) FaultInjectionFilter::CallData(args);
  // Fetch the fault injection policy from the service config, based on the
  // relative index for which policy should this CallData use.
  auto* service_config_call_data = static_cast<ServiceConfigCallData*>(
      args->context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  auto* method_params = static_cast<ClientChannelMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          internal::ClientChannelServiceConfigParser::ParserIndex()));
  calld->fi_policy_ = method_params->fault_injection_policy(chand->index());
  return GRPC_ERROR_NONE;
}

void FaultInjectionFilter::CallData::Destroy(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*then_schedule_closure*/) {
  auto* calld = static_cast<FaultInjectionFilter::CallData*>(elem->call_data);
  calld->~CallData();
}

void FaultInjectionFilter::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  auto* calld = static_cast<FaultInjectionFilter::CallData*>(elem->call_data);
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
    grpc_error* abort_error = calld->MaybeAbort();
    if (abort_error != GRPC_ERROR_NONE) {
      grpc_transport_stream_op_batch_finish_with_failure(batch, abort_error,
                                                         calld->call_combiner_);
      return;
    }
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, batch);
}

FaultInjectionFilter::CallData::CallData(const grpc_call_element_args* args)
    : owning_call_(args->call_stack),
      arena_(args->arena),
      call_combiner_(args->call_combiner) {}

FaultInjectionFilter::CallData::~CallData() {
  if (fi_policy_owned_) {
    fi_policy_->~FaultInjectionPolicy();
    fi_policy_owned_ = false;
  }
}

void FaultInjectionFilter::CallData::DecideWhetherToInjectFaults(
    grpc_metadata_batch* initial_metadata) {
  ClientChannelMethodParsedConfig::FaultInjectionPolicy* copied_policy =
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
            arena_->New<ClientChannelMethodParsedConfig::FaultInjectionPolicy>(
                *fi_policy_);
      }
    };
    for (grpc_linked_mdelem* md = initial_metadata->list.head; md != nullptr;
         md = md->next) {
      absl::string_view key = StringViewFromSlice(GRPC_MDKEY(md->md));
      // Only perform string comparison if:
      //   1. Needs to check this header;
      //   2. The value is not been filled before.
      if (!fi_policy_->abort_code_header.empty() &&
          (copied_policy == nullptr ||
           copied_policy->abort_code == GRPC_STATUS_OK) &&
          key == fi_policy_->abort_code_header) {
        maybe_copy_policy_func();
        grpc_status_code_from_int(GetLinkedMetadatumValueInt(md),
                                  &copied_policy->abort_code);
      }
      if (!fi_policy_->abort_percentage_header.empty() &&
          key == fi_policy_->abort_percentage_header) {
        maybe_copy_policy_func();
        copied_policy->abort_per_million = TranslatePercentageToPerMillion(
            GetLinkedMetadatumValueInt(md),
            fi_policy_->abort_percentage_denominator,
            fi_policy_->abort_per_million);
      }
      if (!fi_policy_->delay_header.empty() &&
          (copied_policy == nullptr || copied_policy->delay == 0) &&
          key == fi_policy_->delay_header) {
        maybe_copy_policy_func();
        copied_policy->delay = static_cast<grpc_millis>(
            GPR_MAX(GetLinkedMetadatumValueInt64(md), 0));
      }
      if (!fi_policy_->delay_percentage_header.empty() &&
          key == fi_policy_->delay_percentage_header) {
        maybe_copy_policy_func();
        copied_policy->delay_per_million = TranslatePercentageToPerMillion(
            GetLinkedMetadatumValueInt(md),
            fi_policy_->delay_percentage_denominator,
            fi_policy_->delay_per_million);
      }
    }
    if (copied_policy != nullptr) fi_policy_ = copied_policy;
  }
  // Roll the dice
  delay_request_ =
      fi_policy_->delay != 0 && UnderFraction(fi_policy_->delay_per_million);
  abort_request_ = fi_policy_->abort_code != GRPC_STATUS_OK &&
                   UnderFraction(fi_policy_->abort_per_million);
  if (!delay_request_ && !abort_request_) {
    if (copied_policy != nullptr) copied_policy->~FaultInjectionPolicy();
    // No fault injection for this call
  } else {
    fi_policy_owned_ = copied_policy != nullptr;
  }
}

bool FaultInjectionFilter::CallData::HaveActiveFaultsQuota(bool increment) {
  if (g_active_faults.Load(MemoryOrder::ACQUIRE) >= fi_policy_->max_faults) {
    return false;
  }
  if (increment) g_active_faults.FetchAdd(1, MemoryOrder::RELAXED);
  return true;
}

bool FaultInjectionFilter::CallData::MaybeDelay() {
  if (delay_request_) {
    return HaveActiveFaultsQuota(true);
  }
  return false;
}

grpc_error* FaultInjectionFilter::CallData::MaybeAbort() {
  if (abort_request_ && (delay_request_ || HaveActiveFaultsQuota(false))) {
    return grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING(fi_policy_->abort_message.c_str()),
        GRPC_ERROR_INT_GRPC_STATUS, fi_policy_->abort_code);
  }
  return GRPC_ERROR_NONE;
}

void FaultInjectionFilter::CallData::DelayBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  MutexLock lock(&delay_mu_);
  delayed_batch_ = batch;
  resume_batch_canceller_ = new ResumeBatchCanceller(elem);
  grpc_millis resume_time = ExecCtx::Get()->Now() + fi_policy_->delay;
  GRPC_CLOSURE_INIT(&batch->handler_private.closure, ResumeBatch, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&delay_timer_, resume_time, &batch->handler_private.closure);
}

void FaultInjectionFilter::CallData::ResumeBatch(void* arg, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  auto* calld = static_cast<FaultInjectionFilter::CallData*>(elem->call_data);
  MutexLock lock(&calld->delay_mu_);
  // Cancelled or canceller has already run
  if (error == GRPC_ERROR_CANCELLED ||
      calld->resume_batch_canceller_ == nullptr) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_fault_injection_filter_trace)) {
    gpr_log(GPR_INFO, "chand %p calld %p: Resuming delayed stream op batch %p",
            elem->channel_data, calld, calld->delayed_batch_);
  }
  // Lame the canceller
  calld->resume_batch_canceller_ = nullptr;
  // Finish fault injection.
  calld->FaultInjectionFinished();
  // Abort if needed.
  error = calld->MaybeAbort();
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_stream_op_batch_finish_with_failure(
        calld->delayed_batch_, error, calld->call_combiner_);
    return;
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, calld->delayed_batch_);
}

}  // namespace

extern const grpc_channel_filter grpc_fault_injection_filter = {
    FaultInjectionFilter::CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(FaultInjectionFilter::CallData),
    FaultInjectionFilter::CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    FaultInjectionFilter::CallData::Destroy,
    sizeof(FaultInjectionFilter::ChannelData),
    FaultInjectionFilter::ChannelData::Init,
    FaultInjectionFilter::ChannelData::Destroy,
    grpc_channel_next_get_info,
    "fault_injection_filter",
};

}  // namespace grpc_core
