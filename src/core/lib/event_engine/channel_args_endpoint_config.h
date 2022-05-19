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

namespace grpc_event_engine {
namespace experimental {

/// A readonly \a EndpointConfig based on grpc_channel_args. This class does not
/// take ownership of the grpc_endpoint_args*, and instances of this class
/// should not be used after the underlying args are destroyed.
class ChannelArgsEndpointConfig : public EndpointConfig {
 public:
  explicit ChannelArgsEndpointConfig(const grpc_channel_args* args)
      : args_(args) {}
  Setting Get(absl::string_view key) const override;

 private:
  const grpc_channel_args* args_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_CHANNEL_ARGS_ENDPOINT_CONFIG_H
