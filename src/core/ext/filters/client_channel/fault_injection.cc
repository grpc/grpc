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

#include "absl/strings/numbers.h"

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/lib/channel/status_util.h"
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

}  // namespace

FaultInjectionData* FaultInjectionData::MaybeCreateFaultInjectionData(
    const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy,
    grpc_metadata_batch* initial_metadata, Atomic<uint32_t>* active_faults,
    Arena* arena) {
  ClientChannelMethodParsedConfig::FaultInjectionPolicy* copied_policy =
      nullptr;
  // Update the policy with values in initial metadata.
  if (!GRPC_SLICE_IS_EMPTY(fi_policy->abort_code_header) ||
      !GRPC_SLICE_IS_EMPTY(fi_policy->abort_per_million_header) ||
      !GRPC_SLICE_IS_EMPTY(fi_policy->delay_header) ||
      !GRPC_SLICE_IS_EMPTY(fi_policy->delay_per_million_header)) {
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
      grpc_slice key = GRPC_MDKEY(md->md);
      // Only perform string comparison if:
      //   1. Needs to check this header;
      //   2. The value is not been filled before.
      if (!GRPC_SLICE_IS_EMPTY(fi_policy->abort_code_header)) {
        if (copied_policy == nullptr ||
            copied_policy->abort_code == GRPC_STATUS_OK) {
          if (grpc_slice_cmp(key, fi_policy->abort_code_header) == 0) {
            maybe_copy_policy_func();
            copied_policy->abort_code =
                grpc_status_code_from_int(GetLinkedMetadatumValueInt(md));
          }
        }
      }
      if (!GRPC_SLICE_IS_EMPTY(fi_policy->abort_per_million_header)) {
        if (copied_policy == nullptr || copied_policy->abort_per_million == 0) {
          if (grpc_slice_cmp(key, fi_policy->abort_per_million_header) == 0) {
            maybe_copy_policy_func();
            copied_policy->abort_per_million =
                GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
          }
        }
      }
      if (!GRPC_SLICE_IS_EMPTY(fi_policy->delay_header)) {
        if (copied_policy == nullptr || copied_policy->delay == 0) {
          if (grpc_slice_cmp(key, fi_policy->delay_header) == 0) {
            maybe_copy_policy_func();
            copied_policy->delay = static_cast<grpc_millis>(
                GPR_MAX(GetLinkedMetadatumValueInt64(md), 0));
          }
        }
      }
      if (!GRPC_SLICE_IS_EMPTY(fi_policy->delay_per_million_header)) {
        if (copied_policy == nullptr || copied_policy->delay_per_million == 0) {
          if (grpc_slice_cmp(key, fi_policy->delay_per_million_header) == 0) {
            maybe_copy_policy_func();
            copied_policy->delay_per_million =
                GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
          }
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
  if (fi_data != nullptr && copied_policy != nullptr)
    fi_data->needs_dealloc_fi_policy_ = true;
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
  if (needs_dealloc_fi_policy_ && fi_policy_ != nullptr) {
    fi_policy_->~FaultInjectionPolicy();
    fi_policy_ = nullptr;
  }
}

bool FaultInjectionData::MaybeDelay() {
  if (delay_request_) {
    return HaveActiveFaultsQuota();
  }
  return false;
}

grpc_error* FaultInjectionData::MaybeAbortError() {
  if (abort_request_ && HaveActiveFaultsQuota()) {
    return grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING(fi_policy_->abort_message.c_str()),
        GRPC_ERROR_INT_GRPC_STATUS, fi_policy_->abort_code);
  }
  return GRPC_ERROR_NONE;
}

void FaultInjectionData::DelayPick(grpc_closure* pick_closure) {
  grpc_millis pick_again_time = ExecCtx::Get()->Now() + fi_policy_->delay;
  grpc_timer_init(&delay_timer_, pick_again_time, pick_closure);
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
