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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {
namespace internal {

namespace {

// TokenBucket is a minimum implementation of token bucket algorithm used for
// response rate limit fault injection. Each token represents 1024 bytes of
// response message allowance.
//
// Since Core ensures there will be only one pending message read, this
// implementation won't need to be thread-safe.
class TokenBucket {
 public:
  TokenBucket(uint32_t token_per_second)
      : tokens_per_second_(token_per_second),
        last_peek_(ExecCtx::Get()->Now()){};
  ~TokenBucket(){};

  // Returns true if tokens are consumed.
  bool ConsumeTokens(double consuiming);
  // Returns the timestamp when the needed tokens are generated.
  grpc_millis TimeUntilNeededTokens(double need);
  // Convert number of bytes into tokens
  inline static double BytesToTokens(uint32_t bytes);

  static double MAX_TOKENS;

 private:
  void UpdateTokens();
  double tokens_per_second_;
  double tokens_ = 0;
  grpc_millis last_peek_;
};

}  // namespace

// FaultInjectionData contains configs for fault injection enforcement.
// Its scope is per-call and it should share the lifespan of the attaching call.
// This class will be used to:
//   1. Update FI configs from other sources;
//   2. Roll the fault-injection dice;
//   3. Maintain the counter of active faults.
class FaultInjectionData {
 public:
  // Creates a FaultInjectionData if this RPC is selected by the policy;
  // otherwise, returns a nullptr.
  // Note that, the fault injection might not be enforced later due to cases:
  //   1. Too many active faults;
  //   2. RPC ended prematurely.
  static FaultInjectionData* MaybeCreateFaultInjectionData(
      const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy,
      grpc_metadata_batch* initial_metadata, Arena* arena);

  FaultInjectionData();
  ~FaultInjectionData();

  bool MaybeAbort();
  bool MaybeDelay();
  bool MaybeRateLimit();

  grpc_error* GetAbortError();
  void ScheduleNextPick(grpc_closure* closure);
  void ThrottleRecvMessageCallback(uint32_t message_length,
                                   grpc_closure* closure, grpc_error* error);

 private:
  // Modifies internal states to when fault injection actually starts, also
  // checks if current active faults exceed the allowed max faults.
  bool BeginFaultInjection();
  // EndFaultInjection maybe called multiple time to stop the fault injection.
  bool EndFaultInjection();
  bool active_fault_increased_ = false;
  bool active_fault_decreased_ = false;
  const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy_;
  // Flag if each fault injection is enabled
  bool abort_request_ = false;
  bool delay_request_ = false;
  bool rate_limit_response_ = false;
  // Delay statuses
  bool delay_injected_ = false;
  bool delay_finished_ = false;
  grpc_timer delay_timer_;
  grpc_millis pick_again_time_;
  // Abort statuses
  bool abort_injected_ = false;
  bool abort_finished_ = false;
  // Response rate limit status
  bool rate_limit_started_ = false;
  bool rate_limit_finished_ = false;
  TokenBucket* rate_limit_bucket_ = nullptr;
  grpc_timer callback_postpone_timer_;
};

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_H */
