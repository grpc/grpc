// Copyright 2021 The gRPC Authors
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
#ifndef GRPC_EVENT_ENGINE_ENDPOINT_CONFIG_H
#define GRPC_EVENT_ENGINE_ENDPOINT_CONFIG_H

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/string_view.h"

#include "absl/types/variant.h"

namespace grpc_event_engine {
namespace experimental {

/// An map of key-value pairs representing Endpoint configuration options.
///
/// This class does not take ownership of any pointers passed to it.
class EndpointConfig {
 public:
  using Setting = absl::variant<absl::monostate, int, std::string, void*>;
  virtual const Setting Get(absl::string_view key) const = 0;
  virtual ~EndpointConfig() = default;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_ENDPOINT_CONFIG_H
