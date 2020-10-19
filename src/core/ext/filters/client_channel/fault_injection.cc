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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_CC
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_CC

#include <math.h>

#include "src/core/ext/filters/client_channel/fault_injection.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include "absl/strings/numbers.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/metadata_batch.h"

#define HEADER_FAULT_ABORT_GRPC_REQUEST "x-envoy-fault-abort-grpc-request"
#define HEADER_FAULT_ABORT_REQUEST_PERCENTAGE "x-envoy-fault-abort-percentage"
#define HEADER_FAULT_DELAY_REQUEST "x-envoy-fault-delay-request"
#define HEADER_FAULT_DELAY_REQUEST_PERCENTAGE \
  "x-envoy-fault-delay-request-percentage"
#define HEADER_FAULT_THROUGHPUT_RESPONSE "x-envoy-fault-throughput-response"
#define HEADER_FAULT_THROUGHPUT_RESPONSE_PERCENTAGE \
  "x-envoy-fault-throughput-response-percentage"

namespace grpc_core {
namespace internal {

namespace {

std::atomic<uint32_t> g_active_faults(0);

inline int GetLinkedMetadatumValueInt(grpc_linked_mdelem* md) {
  int res;
  if (absl::SimpleAtoi(StringViewFromSlice(GRPC_MDVALUE(md->md)), &res)) {
    return res;
  } else {
    return 0;
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

double TokenBucket::MAX_TOKENS = std::numeric_limits<uint32_t>::max();

inline double TokenBucket::BytesToTokens(uint32_t bytes) {
  return ceil(double(bytes) / 1024);
}

void TokenBucket::UpdateTokens() {
  // Don't let tokens overflow!
  if (tokens_ < MAX_TOKENS) {
    grpc_millis now = ExecCtx::Get()->Now();
    double spawned = double(now - last_peek_) / 1000 * tokens_per_second_;
    if (spawned + tokens_ < MAX_TOKENS) {
      tokens_ += spawned;
    } else {
      tokens_ = MAX_TOKENS;
    }
    last_peek_ = now;
  }
}

bool TokenBucket::ConsumeTokens(double consuming) {
  UpdateTokens();
  // Panic if there are multiple token preconsumption.
  GPR_ASSERT(tokens_ >= 0);
  if (tokens_ >= consuming) {
    tokens_ -= consuming;
    return true;
  }
  return false;
}

grpc_millis TokenBucket::TimeUntilNeededTokens(double need) {
  UpdateTokens();
  if (need <= tokens_) return 0;
  double deficit = need - tokens_;
  // Preconsume the tokens.
  tokens_ = -deficit;
  return ExecCtx::Get()->Now() +
         static_cast<grpc_millis>((double(deficit) / tokens_per_second_) *
                                  1000);
}

}  // namespace

FaultInjectionData* FaultInjectionData::MaybeCreateFaultInjectionData(
    const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy,
    grpc_metadata_batch* initial_metadata, Arena* arena) {
  // Update the policy with values in initial metadata.
  if (fi_policy->delay_by_headers || fi_policy->abort_by_headers ||
      fi_policy->rate_limit_by_headers) {
    ClientChannelMethodParsedConfig::FaultInjectionPolicy* copied_policy =
        nullptr;
    // Defer the actual copy until the first matched header.
    auto CopyPolicy = [&copied_policy, fi_policy, arena]() {
      copied_policy =
          (ClientChannelMethodParsedConfig::FaultInjectionPolicy*)arena->Alloc(
              sizeof(ClientChannelMethodParsedConfig::FaultInjectionPolicy));
      *copied_policy = *fi_policy;
    };
    GRPC_ITERATE_MD(initial_metadata->list.head, md) {
      absl::string_view key = StringViewFromSlice(GRPC_MDKEY(md->md));
      if (fi_policy->abort_by_headers) {
        // Checks if the overriding field is empty, if not, it means that it
        // has been filled by previous headers. With this optimization, we
        // can skip several string comparison.
        if (copied_policy->abort_code == GRPC_STATUS_OK &&
            key == HEADER_FAULT_ABORT_GRPC_REQUEST) {
          if (copied_policy == nullptr) CopyPolicy();
          int candidate = GetLinkedMetadatumValueInt(md);
          if (candidate < 0 || candidate > 15) {
            copied_policy->abort_code = GRPC_STATUS_UNKNOWN;
          } else {
            copied_policy->abort_code =
                static_cast<grpc_status_code>(candidate);
          }
        } else if (copied_policy->abort_per_million == 0 &&
                   key == HEADER_FAULT_ABORT_REQUEST_PERCENTAGE) {
          if (copied_policy == nullptr) CopyPolicy();
          copied_policy->abort_per_million =
              GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
        }
      }
      if (fi_policy->delay_by_headers) {
        if (copied_policy->delay == 0 && key == HEADER_FAULT_DELAY_REQUEST) {
          if (copied_policy == nullptr) CopyPolicy();
          copied_policy->delay = static_cast<grpc_millis>(
              GPR_MAX(GetLinkedMetadatumValueInt64(md), 0));
        } else if (copied_policy->delay_per_million == 0 &&
                   key == HEADER_FAULT_DELAY_REQUEST_PERCENTAGE) {
          if (copied_policy == nullptr) CopyPolicy();
          copied_policy->delay_per_million =
              GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
        }
      }
      if (fi_policy->rate_limit_by_headers) {
        if (copied_policy->per_stream_response_rate_limit == 0 &&
            key == HEADER_FAULT_THROUGHPUT_RESPONSE) {
          if (copied_policy == nullptr) CopyPolicy();
          copied_policy->per_stream_response_rate_limit =
              GPR_MAX(GetLinkedMetadatumValueInt(md), 0);
        } else if (copied_policy->response_rate_limit_per_million == 0 &&
                   key == HEADER_FAULT_THROUGHPUT_RESPONSE_PERCENTAGE) {
          if (copied_policy == nullptr) CopyPolicy();
          copied_policy->response_rate_limit_per_million =
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
  if (fi_policy->per_stream_response_rate_limit != 0 &&
      UnderFraction(fi_policy->response_rate_limit_per_million)) {
    if (fi_data == nullptr) fi_data = arena->New<FaultInjectionData>();
    fi_data->rate_limit_response_ = true;
    // Unit of per_stream_response_rate_limit is kbps, which equals to tokens
    // per second.
    fi_data->rate_limit_bucket_ =
        arena->New<TokenBucket>(fi_policy->per_stream_response_rate_limit);
  }
  if (fi_data != nullptr) fi_data->fi_policy_ = fi_policy;
  return fi_data;
}

FaultInjectionData::FaultInjectionData() {}

FaultInjectionData::~FaultInjectionData() {
  // If this RPC is fault injected but the injection wasn't finished, we need
  // to correct the active faults counting.
  if (active_fault_increased_ && !active_fault_decreased_) --g_active_faults;
}

bool FaultInjectionData::MaybeDelay() {
  if (!delay_request_ || delay_finished_) return false;
  // Delay has been injected by previous invocation of PickSubchannel, check if
  // the delay has finished.
  if (delay_injected_) {
    if (ExecCtx::Get()->Now() >= pick_again_time_) {
      delay_finished_ = true;
      EndFaultInjection();
    }
    return false;
  }
  delay_injected_ = true;
  // If active faults count allow, return true to delay the request
  if (BeginFaultInjection()) return true;
  // Otherwise, skip the delay injection.
  delay_finished_ = true;
  return false;
}

bool FaultInjectionData::MaybeAbort() {
  if (!abort_request_ || abort_finished_) return false;
  if (abort_injected_) {
    abort_finished_ = true;
    EndFaultInjection();
    return false;
  }
  abort_injected_ = true;
  // This RPC only shortly counts as active fault before the dtor invocation,
  // or before the next retry.
  if (BeginFaultInjection()) return true;
  abort_finished_ = true;
  return false;
}

bool FaultInjectionData::MaybeRateLimit() {
  if (!rate_limit_response_ || rate_limit_finished_) return false;
  // Once started, the response rate limit will apply to the entire RPC
  // lifespan, even if it retries.
  if (rate_limit_started_) return true;
  rate_limit_started_ = true;
  // The rate limit fault will be counted as an active fault until the end of
  // the entire RPC.
  if (BeginFaultInjection()) return true;
  rate_limit_finished_ = true;
  return false;
}

void FaultInjectionData::ThrottleRecvMessageCallback(uint32_t message_length,
                                                     grpc_closure* closure,
                                                     grpc_error* error) {
  uint32_t needed_tokens = TokenBucket::BytesToTokens(message_length);
  if (rate_limit_bucket_->ConsumeTokens(needed_tokens)) {
    Closure::Run(DEBUG_LOCATION, closure, GRPC_ERROR_REF(error));
  } else {
    grpc_millis wait_until =
        rate_limit_bucket_->TimeUntilNeededTokens(needed_tokens);
    grpc_timer_init(&callback_postpone_timer_, wait_until, closure);
  }
}

grpc_error* FaultInjectionData::GetAbortError() {
  return grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_COPIED_STRING(fi_policy_->abort_message.c_str()),
      GRPC_ERROR_INT_GRPC_STATUS, fi_policy_->abort_code);
}

void FaultInjectionData::ScheduleNextPick(grpc_closure* closure) {
  pick_again_time_ = ExecCtx::Get()->Now() + fi_policy_->delay;
  grpc_timer_init(&delay_timer_, pick_again_time_, closure);
}

bool FaultInjectionData::BeginFaultInjection() {
  if (g_active_faults >= fi_policy_->max_faults) return false;
  // One RPC is not allowed to be counted twice as active fault.
  GPR_ASSERT(!active_fault_decreased_);
  if (!active_fault_increased_) {
    active_fault_increased_ = true;
    ++g_active_faults;
  }
  return true;
}

bool FaultInjectionData::EndFaultInjection() {
  // TODO(lidiz) check rate limit response status
  if (delay_request_ && !delay_finished_) return false;
  if (abort_request_ && !abort_finished_) return false;
  if (!active_fault_decreased_) {
    active_fault_decreased_ = true;
    g_active_faults--;
  }
  return true;
}

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_CC */
