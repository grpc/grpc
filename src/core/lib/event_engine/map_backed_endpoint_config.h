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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H
#define GRPC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H

#include <grpc/support/port_platform.h>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/impl/codegen/grpc_types.h>

namespace grpc_event_engine {
namespace experimental {

/// A readonly \a EndpointConfig based on an absl::flat_hash_map.
class MapBackedEndpointConfig : public EndpointConfig {
 public:
  MapBackedEndpointConfig() {}
  MapBackedEndpointConfig(const MapBackedEndpointConfig& other) = default;
  MapBackedEndpointConfig& operator=(const MapBackedEndpointConfig&) = default;
  // Return the value of the specified key.
  Setting Get(absl::string_view key) const override;
  // Copy the value corresponding to the specified key and insert it into the
  // map.
  void CopyFrom(const EndpointConfig& config, absl::string_view key);
  // Insert the value at the specified key.
  void Insert(absl::string_view key, Setting value);

 private:
  absl::flat_hash_map<absl::string_view, EndpointConfig::Setting> config_map_;
};

using ConfigMap = MapBackedEndpointConfig;

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H
