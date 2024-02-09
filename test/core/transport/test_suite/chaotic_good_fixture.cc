// Copyright 2023 gRPC authors.
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

#include <memory>

#include "gmock/gmock.h"

#include "src/core/ext/transport/chaotic_good/client_transport.h"
#include "src/core/ext/transport/chaotic_good/server_transport.h"
#include "src/core/lib/event_engine/memory_allocator_factory.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "test/core/transport/test_suite/fixture.h"

using grpc_event_engine::experimental::EndpointConfig;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;
using grpc_event_engine::experimental::MemoryQuotaBasedMemoryAllocatorFactory;
using grpc_event_engine::experimental::URIToResolvedAddress;

namespace grpc_core {

namespace {

class MockEndpointConfig : public EndpointConfig {
 public:
  MOCK_METHOD(absl::optional<int>, GetInt, (absl::string_view key),
              (const, override));
  MOCK_METHOD(absl::optional<absl::string_view>, GetString,
              (absl::string_view key), (const, override));
  MOCK_METHOD(void*, GetVoidPointer, (absl::string_view key),
              (const, override));
};

struct EndpointPair {
  PromiseEndpoint client;
  PromiseEndpoint server;
};

EndpointPair CreateEndpointPair(
    grpc_event_engine::experimental::FuzzingEventEngine* event_engine,
    ResourceQuotaRefPtr resource_quota, int port) {
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
  GPR_ASSERT(listener->Bind(resolved_address).ok());
  GPR_ASSERT(listener->Start().ok());

  event_engine->Connect(
      [&client_endpoint](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
        GPR_ASSERT(endpoint.ok());
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

}  // namespace

TRANSPORT_FIXTURE(ChaoticGood) {
  auto resource_quota = MakeResourceQuota("test");
  EndpointPair control_endpoints =
      CreateEndpointPair(event_engine.get(), resource_quota, 1234);
  EndpointPair data_endpoints =
      CreateEndpointPair(event_engine.get(), resource_quota, 4321);
  auto channel_args =
      ChannelArgs()
          .SetObject(resource_quota)
          .SetObject(std::static_pointer_cast<EventEngine>(event_engine));
  auto client_transport =
      MakeOrphanable<chaotic_good::ChaoticGoodClientTransport>(
          std::move(control_endpoints.client), std::move(data_endpoints.client),
          ChannelArgs().SetObject(resource_quota), event_engine, HPackParser(),
          HPackCompressor());
  auto server_transport =
      MakeOrphanable<chaotic_good::ChaoticGoodServerTransport>(
          channel_args, std::move(control_endpoints.server),
          std::move(data_endpoints.server), event_engine, HPackParser(),
          HPackCompressor());
  return ClientAndServerTransportPair{std::move(client_transport),
                                      std::move(server_transport)};
}

}  // namespace grpc_core
