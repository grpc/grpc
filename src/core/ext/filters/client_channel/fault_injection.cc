//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/fault_injection.h"

#include <grpc/support/alloc.h>

#include "absl/strings/numbers.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {
namespace internal {

namespace {

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

inline bool NeedsHeaderMatching(
    const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy) {
  return !fi_policy->abort_code_header.empty() ||
         !fi_policy->abort_per_million_header.empty() ||
         !fi_policy->delay_header.empty() ||
         !fi_policy->delay_per_million_header.empty();
}

}  // namespace

FaultInjectionData* FaultInjectionData::MaybeCreateFaultInjectionData(
    const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy,
    grpc_metadata_batch* initial_metadata, Atomic<uint32_t>* active_faults,
    Arena* arena) {
  // Update the policy with values in initial metadata.
  if (NeedsHeaderMatching(fi_policy)) {
    ClientChannelMethodParsedConfig::FaultInjectionPolicy* copied_policy =
        nullptr;
    // Defer the actual copy until the first matched header.
    auto maybe_copy_policy_func = [&copied_policy, fi_policy, arena]() {
      if (copied_policy == nullptr) {
        copied_policy =
            arena->New<ClientChannelMethodParsedConfig::FaultInjectionPolicy>(
                *fi_policy);
      }
    };
    for (grpc_linked_mdelem* md = initial_metadata->list.head; md != nullptr;
         md = md->next) {
      absl::string_view key = StringViewFromSlice(GRPC_MDKEY(md->md));
      // Only perform string comparison if:
      //   1. Needs to check this header;
      //   2. The value is not been filled before.
      if (!fi_policy->abort_code_header.empty()) {
        if (copied_policy != nullptr &&
            copied_policy->abort_code != GRPC_STATUS_OK) {
          continue;
        }
        if (key == fi_policy->abort_code_header) {
          maybe_copy_policy_func();
          int candidate = GetLinkedMetadatumValueInt(md);
          if (candidate < GRPC_STATUS_OK || candidate > GRPC_STATUS_DATA_LOSS) {
            copied_policy->abort_code = GRPC_STATUS_UNKNOWN;
          } else {
            copied_policy->abort_code =
                static_cast<grpc_status_code>(candidate);
          }
        }
      }
      if (!fi_policy->abort_per_million_header.empty()) {
        if (copied_policy != nullptr && copied_policy->abort_per_million != 0) {
          continue;
        }
        if (key == fi_policy->abort_per_million_header) {
          maybe_copy_policy_func();
          copied_policy->abort_per_million =
              GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
        }
      }
      if (!fi_policy->delay_header.empty()) {
        if (copied_policy != nullptr && copied_policy->delay != 0) continue;
        if (key == fi_policy->delay_header) {
          maybe_copy_policy_func();
          copied_policy->delay = static_cast<grpc_millis>(
              GPR_MAX(GetLinkedMetadatumValueInt64(md), 0));
        }
      }
      if (!fi_policy->delay_per_million_header.empty()) {
        if (copied_policy != nullptr && copied_policy->delay_per_million != 0) {
          continue;
        }
        if (key == fi_policy->delay_per_million_header) {
          maybe_copy_policy_func();
          copied_policy->delay_per_million =
              GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
        }
      }
    }
    if (copied_policy != nullptr) fi_policy = copied_policy;
  }
  // Roll the dice
  FaultInjectionData* fi_data = nullptr;
  if (fi_policy->abort_code != GRPC_STATUS_OK &&
      UnderFraction(fi_policy->abort_per_million)) {
    if (fi_data == nullptr) fi_data = arena->New<FaultInjectionData>();
    fi_data->abort_request_ = true;
  }
  if (fi_policy->delay != 0 && UnderFraction(fi_policy->delay_per_million)) {
    if (fi_data == nullptr) fi_data = arena->New<FaultInjectionData>();
    fi_data->delay_request_ = true;
  }
  if (fi_data != nullptr) fi_data->fi_policy_ = fi_policy;
  if (fi_data != nullptr) fi_data->active_faults_ = active_faults;
  return fi_data;
}

void FaultInjectionData::Destroy() {
  // If this RPC is fault injected but the injection wasn't finished, we need
  // to correct the active faults counting.
  if (active_fault_increased_ && !active_fault_decreased_) {
    active_fault_decreased_ = true;
    active_faults_->FetchSub(1, MemoryOrder::RELAXED);
  }
}

bool FaultInjectionData::MaybeDelay() {
  if (delay_request_) {
    return HaveActiveFaultsQuota();
  }
  return false;
}

bool FaultInjectionData::MaybeAbort() {
  if (abort_request_) {
    return HaveActiveFaultsQuota();
  }
  return false;
}

grpc_error* FaultInjectionData::GetAbortError() {
  return grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_COPIED_STRING(fi_policy_->abort_message.c_str()),
      GRPC_ERROR_INT_GRPC_STATUS, fi_policy_->abort_code);
}

void FaultInjectionData::ResumePick(void* arg, grpc_error* error) {
  auto* self = static_cast<FaultInjectionData*>(arg);
  if (error == GRPC_ERROR_NONE && self->MaybeAbort()) {
    error = self->GetAbortError();
  }
  Closure::Run(DEBUG_LOCATION, self->pick_closure_, error);
  // Ends the fault injection because both delay and abort injection are
  // finished.
  self->Destroy();
}

void FaultInjectionData::DelayPick(grpc_closure* pick_closure) {
  // TODO(lidiz) Need to figure out why resume pick doesn't work
  // pick_closure_ = pick_closure;
  // GRPC_CLOSURE_INIT(&resume_pick_closure_, &ResumePick, this,
  //                   grpc_schedule_on_exec_ctx);
  pick_again_time_ = ExecCtx::Get()->Now() + fi_policy_->delay;
  // grpc_timer_init(&delay_timer_, pick_again_time_, &resume_pick_closure_);
  grpc_timer_init(&delay_timer_, pick_again_time_, pick_closure);
}

void FaultInjectionData::CancelDelayTimer() {
  grpc_timer_cancel(&delay_timer_);
}

bool FaultInjectionData::HaveActiveFaultsQuota() {
  if (active_faults_->Load(MemoryOrder::ACQUIRE) >= fi_policy_->max_faults) {
    return false;
  }
  if (!active_fault_increased_) {
    active_fault_increased_ = true;
    active_faults_->FetchAdd(1, MemoryOrder::RELAXED);
  }
  return true;
}

}  // namespace internal
}  // namespace grpc_core
