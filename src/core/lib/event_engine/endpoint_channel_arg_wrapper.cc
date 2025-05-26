// Copyright 2025 The gRPC Authors
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

#include "src/core/lib/event_engine/endpoint_channel_arg_wrapper.h"

#include "src/core/util/useful.h"
namespace grpc_event_engine {
namespace experimental {

EndpointChannelArgWrapper::EndpointChannelArgWrapper(
    std::unique_ptr<EventEngine::Endpoint> endpoint)
    : endpoint_(std::move(endpoint)) {}

std::unique_ptr<EventEngine::Endpoint>
EndpointChannelArgWrapper::TakeEndpoint() {
  return std::move(endpoint_);
}

absl::string_view EndpointChannelArgWrapper::ChannelArgName() {
  return "grpc.internal.subchannel_endpoint";
}

int EndpointChannelArgWrapper::ChannelArgsCompare(
    const EndpointChannelArgWrapper* a, const EndpointChannelArgWrapper* b) {
  return QsortCompare(a, b);
}

}  // namespace experimental
}  // namespace grpc_event_engine