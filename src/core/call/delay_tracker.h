// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_DELAY_TRACKER_H
#define GRPC_SRC_CORE_CALL_DELAY_TRACKER_H

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/time.h"

namespace grpc_core {

// Tracks delays seen while processing a client call.  This information
// is added to the status message when a call fails with DEADLINE_EXCEEDED.
class DelayTracker {
 public:
  using Handle = size_t;

  // Starts recording a delay.  Returns a handle for the new delay.  The
  // caller needs to hold on to the handle and later pass it to EndDelay()
  // when the delay is complete.
  Handle StartDelay(absl::string_view description);

  // Ends a delay.
  void EndDelay(Handle handle);

  // Reports delay info in a form suitable for inclusion in a status message.
  std::string GetDelayInfo();

 private:
  struct Delay {
    std::string description;
    Timestamp start = Timestamp::Now();
    Timestamp end = Timestamp::InfPast();

    explicit Delay(absl::string_view desc) : description(desc) {}
  };

  std::vector<Delay> delays_;
};

// Allow DelayTracker to be used as an arena context element.
template <>
struct ArenaContextType<DelayTracker> {
  static void Destroy(DelayTracker* ptr) { ptr->~DelayTracker(); }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_DELAY_TRACKER_H
