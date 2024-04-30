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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_event_engine {
namespace experimental {

class ChannelArgsEndpointConfig : public EndpointConfig {
 public:
  ChannelArgsEndpointConfig() = default;
  explicit ChannelArgsEndpointConfig(const grpc_core::ChannelArgs& args)
      : args_(args) {}
  ChannelArgsEndpointConfig(const ChannelArgsEndpointConfig& config) = default;
  ChannelArgsEndpointConfig& operator=(const ChannelArgsEndpointConfig& other) =
      default;
  absl::optional<int> GetInt(absl::string_view key) const override;
  absl::optional<absl::string_view> GetString(
      absl::string_view key) const override;
  void* GetVoidPointer(absl::string_view key) const override;

 private:
  grpc_core::ChannelArgs args_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H
