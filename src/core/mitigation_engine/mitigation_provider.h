//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_PROVIDER_H
#define GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_PROVIDER_H

#include <cstdint>
#include <vector>

#include "src/core/mitigation_engine/mitigation.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/functional/any_invocable.h"

namespace grpc_core {

// Opaque handle for unsubscription.
struct MitigationSubscriptionHandle {
  uintptr_t id = 0;
  bool operator==(const MitigationSubscriptionHandle& other) const = default;
};

class MitigationProvider {
 public:
  virtual ~MitigationProvider() = default;

  // Subscribes to mitigation configuration updates.
  //
  // The provided callback is invoked whenever the mitigation rules change.
  //
  // Returns an opaque MitigationSubscriptionHandle to be used as an
  // unsubscription token for CancelWatch().
  //
  // Mitigations are passed as RefCountedPtrs to enable efficient shared
  // ownership of the read-only rules among all listeners, avoiding costly
  // deep copies during config updates.
  virtual MitigationSubscriptionHandle ListenForMitigations(
      absl::AnyInvocable<void(std::vector<RefCountedPtr<Mitigation>>)>
          new_mitigations) = 0;

  // Unsubscribes a previously registered callback using its original
  // MitigationSubscriptionHandle token.
  virtual void CancelWatch(MitigationSubscriptionHandle handle) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_MITIGATION_ENGINE_MITIGATION_PROVIDER_H
