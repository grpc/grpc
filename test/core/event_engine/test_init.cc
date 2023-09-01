// Copyright 2022 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#include "test/core/event_engine/test_init.h"

#include "absl/strings/str_cat.h"

namespace grpc_event_engine {
namespace experimental {

/// Sets the default EventEngine factory, used for testing.
/// Currently the only valid engine is 'default' or ''.
/// When more engines are added, this should be updated accordingly.
absl::Status InitializeTestingEventEngineFactory(absl::string_view engine) {
  if (engine == "default" || engine.empty()) {
    // No-op, the default will be used
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unknown EventEngine implementation: ", engine));
}

}  // namespace experimental
}  // namespace grpc_event_engine
