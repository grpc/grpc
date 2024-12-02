// Copyright 2024 gRPC authors.
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

#include "test/core/transport/test_suite/chaotic_good_fixture_helpers.h"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;
using grpc_event_engine::experimental::MemoryQuotaBasedMemoryAllocatorFactory;
using grpc_event_engine::experimental::URIToResolvedAddress;

namespace grpc_core {

EndpointPair CreateEndpointPair(FuzzingEventEngine* event_engine,
                                ResourceQuota* resource_quota, int port) {
  std::unique_ptr<EventEngine::Endpoint> client_endpoint;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint;

  const auto resolved_address =
      URIToResolvedAddress(absl::StrCat("ipv4:127.0.0.1:", port)).value();

  ::testing::StrictMock<MockEndpointConfig> endpoint_config;
  auto listener = *event_engine->CreateListener(
      [&server_endpoint](std::unique_ptr<EventEngine::Endpoint> endpoint,
                         MemoryAllocator) {
        server_endpoint = std::move(endpoint);
      },
      [](absl::Status) {}, endpoint_config,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          resource_quota->memory_quota()));
  CHECK_OK(listener->Bind(resolved_address));
  CHECK_OK(listener->Start());

  event_engine->Connect(
      [&client_endpoint](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
        CHECK_OK(endpoint);
        client_endpoint = std::move(endpoint).value();
      },
      resolved_address, endpoint_config,
      resource_quota->memory_quota()->CreateMemoryAllocator("client"),
      Duration::Hours(3));

  while (client_endpoint == nullptr || server_endpoint == nullptr) {
    event_engine->Tick();
  }

  return EndpointPair{
      PromiseEndpoint(std::move(client_endpoint), SliceBuffer()),
      PromiseEndpoint(std::move(server_endpoint), SliceBuffer())};
}

}  // namespace grpc_core
