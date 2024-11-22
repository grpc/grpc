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

#ifndef GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_CHAOTIC_GOOD_H
#define GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_CHAOTIC_GOOD_H

#include <memory>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "src/core/ext/transport/chaotic_good/client_transport.h"
#include "src/core/ext/transport/chaotic_good/server_transport.h"
#include "src/core/lib/event_engine/memory_allocator_factory.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "test/core/transport/test_suite/transport_test.h"

using grpc_event_engine::experimental::EndpointConfig;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;
using grpc_event_engine::experimental::MemoryQuotaBasedMemoryAllocatorFactory;
using grpc_event_engine::experimental::URIToResolvedAddress;

namespace grpc_core {

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
    ResourceQuota* resource_quota, int port);

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_CHAOTIC_GOOD_H
