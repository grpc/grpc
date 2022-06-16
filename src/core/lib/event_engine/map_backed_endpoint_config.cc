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

#include "src/core/lib/event_engine/map_backed_endpoint_config.h"

#include <string>

#include "absl/types/variant.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_event_engine {
namespace experimental {

EndpointConfig::Setting MapBackedEndpointConfig::Get(
    absl::string_view key) const {
  auto it = config_map_.find(key);
  if (it == config_map_.end()) {
    return absl::monostate();
  }
  return it->second;
}

void MapBackedEndpointConfig::CopyFrom(const EndpointConfig& config,
                                       absl::string_view key) {
  auto value = config.Get(key);
  if (!absl::holds_alternative<absl::monostate>(value)) {
    Insert(key, value);
  }
}

void MapBackedEndpointConfig::Insert(absl::string_view key,
                                     EndpointConfig::Setting value) {
  config_map_.insert_or_assign(key, value);
}

}  // namespace experimental
}  // namespace grpc_event_engine
