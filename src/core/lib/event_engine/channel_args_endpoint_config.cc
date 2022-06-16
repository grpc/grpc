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

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"

#include <string>

#include "absl/types/variant.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_event_engine {
namespace experimental {

EndpointConfig::EndpointConfig() : impl_(nullptr){};

EndpointConfig::~EndpointConfig() = default;

EndpointConfig::EndpointConfig(
    std::shared_ptr<EndpointConfig::OptionsAccessor> impl)
    : impl_(std::move(impl)){};

EndpointConfig::Setting EndpointConfig::Get(absl::string_view key) const {
  if (impl_ == nullptr) {
    return absl::monostate();
  }
  return impl_->Get(key);
}

EndpointConfig::EndpointConfig(const EndpointConfig& other) {
  impl_ = other.impl_;
}

EndpointConfig& EndpointConfig::operator=(EndpointConfig other) {
  impl_ = other.impl_;
  return *this;
}

EndpointConfig ChannelArgsEndpointConfig(const grpc_core::ChannelArgs& args) {
  return EndpointConfig(
      std::make_shared<EndpointConfig::OptionsAccessor>(args));
}

EndpointConfig ChannelArgsEndpointConfig(const grpc_channel_args* args) {
  return EndpointConfig(
      std::make_shared<EndpointConfig::OptionsAccessor>(args));
}

}  // namespace experimental
}  // namespace grpc_event_engine
