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

/// A set of parameters used to configure an endpoint, either when initiating a
/// new connection on the client side or when listening for incoming connections
/// on the server side. An EndpointConfig contains a set of zero or more
/// Settings. Each setting has a unique name, which can be used to fetch that
/// Setting via the Get() method. Each Setting has a value, which can be an
/// integer, string, or void pointer. Each EE impl should define the set of
/// Settings that it supports being passed into it, along with the corresponding
/// type.
class EndpointConfig {
 public:
  virtual ~EndpointConfig() = default;
  using Setting = absl::variant<absl::monostate, int, absl::string_view, void*>;
  /// Returns an EndpointConfig Setting. If there is no Setting associated with
  /// \a key in the EndpointConfig, an \a absl::monostate type will be
  /// returned. Caller does not take ownership of resulting value.
  virtual Setting Get(absl::string_view key) const = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_ENDPOINT_CONFIG_H
