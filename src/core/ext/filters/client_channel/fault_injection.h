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
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {
namespace internal {

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
  // Note that, the fault injection might not be enforced later due to:
  //   1. Too many active faults;
  //   2. RPC ended prematurely.
  static FaultInjectionData* MaybeCreateFaultInjectionData(
      const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy,
      grpc_metadata_batch* initial_metadata, Arena* arena);

  // Corrects the active faults counter.
  void Destroy();

  FaultInjectionData() = default;
  ~FaultInjectionData() { Destroy(); }

  // Returns true if this RPC needs to be delayed. If so, this call will be
  // counted as an active fault.
  bool MaybeDelay(Atomic<uint32_t>* active_faults);
  // Returns the aborted RPC status if this RPC needs to be aborted. If so,
  // this call will be counted as an active fault. Otherwise, it returns
  // GRPC_ERROR_NONE.
  grpc_error* MaybeAbort(Atomic<uint32_t>* active_faults);

  // Delays the subchannel pick.
  void DelayPick(grpc_closure* pick_closure);
  // Cancels the delay timer.
  void CancelDelayTimer(Atomic<uint32_t>* active_faults);

  // Finishes the delay injection, should only be called once.
  void DelayFinished(Atomic<uint32_t>* active_faults);

 private:
  // Checks if current active faults exceed the allowed max faults.
  bool HaveActiveFaultsQuota(Atomic<uint32_t>* active_faults, bool add_one);

  // A pointer to the per channel active faults counter;
  const ClientChannelMethodParsedConfig::FaultInjectionPolicy* fi_policy_;
  bool fi_policy_owned_ = false;

  // Indicates whether we are doing a delay and/or an abort for this call.
  bool abort_request_ = false;
  bool delay_request_ = false;
  // Delay statuses
  grpc_timer delay_timer_;
};

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_H */
