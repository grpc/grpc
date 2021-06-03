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
#include <grpc/support/port_platform.h>

#include <grpc/event_engine/endpoint_config.h>

#include "absl/container/flat_hash_map.h"
#include "absl/types/variant.h"

namespace grpc_event_engine {
namespace experimental {

EndpointConfig::Setting& EndpointConfig::operator[](const std::string& key) {
  return map_[key];
}

EndpointConfig::Setting& EndpointConfig::operator[](std::string&& key) {
  return map_[key];
}

void EndpointConfig::enumerate(
    std::function<bool(absl::string_view, const Setting&)> cb) {
  for (const auto& item : map_) {
    if (!cb(item.first, item.second)) {
      return;
    }
  }
}

void EndpointConfig::clear() { map_.clear(); }

size_t EndpointConfig::size() { return map_.size(); }

bool EndpointConfig::contains(absl::string_view key) {
  return map_.contains(key);
}

}  // namespace experimental
}  // namespace grpc_event_engine
