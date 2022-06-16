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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H
#define GRPC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_event_engine {
namespace experimental {

class EndpointConfig::OptionsAccessor {
 public:
  OptionsAccessor(const grpc_core::ChannelArgs& args) : args_(args) {}
  OptionsAccessor(const grpc_channel_args* args)
      : args_(grpc_core::ChannelArgs::FromC(args)) {}
  EndpointConfig::Setting Get(absl::string_view key) const {
    auto value = args_.Get(key);
    if (value == nullptr) {
      return absl::monostate();
    }
    if (absl::holds_alternative<grpc_core::ChannelArgs::Pointer>(*value)) {
      return absl::get<grpc_core::ChannelArgs::Pointer>((*value)).c_pointer();
    }

    if (absl::holds_alternative<int>(*value)) {
      return absl::get<int>(*value);
    }

    if (absl::holds_alternative<std::string>(*value)) {
      return absl::get<std::string>(*value);
    }
    GPR_UNREACHABLE_CODE(return absl::monostate());
  }

 private:
  grpc_core::ChannelArgs args_;
};

EndpointConfig ChannelArgsEndpointConfig(const grpc_core::ChannelArgs& args);
EndpointConfig ChannelArgsEndpointConfig(const grpc_channel_args* args);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H
