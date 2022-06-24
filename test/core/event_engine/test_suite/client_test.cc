// Copyright 2022 gRPC authors.
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

#include <random>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/promise.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"
#include "test/core/event_engine/test_suite/event_engine_test_utils.h"
#include "test/core/util/port.h"

class EventEngineClientTest : public EventEngineTest {};

namespace {

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Promise;
using ::grpc_event_engine::experimental::URIToResolvedAddress;
using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
using namespace std::chrono_literals;

constexpr int kMinMessageSize = 1024;
constexpr int kMaxMessageSize = 4096;
constexpr int kNumExchangedMessages = 100;

// Returns a random message with bounded length.
std::string GetNextSendMessage() {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  static std::random_device rd;
  static std::seed_seq seed{rd()};
  static std::mt19937 gen(seed);
  static std::uniform_real_distribution<> dis(kMinMessageSize, kMaxMessageSize);
  static grpc_core::Mutex g_mu;
  std::string tmp_s;
  int len;
  {
    grpc_core::MutexLock lock(&g_mu);
    len = dis(gen);
  }
  tmp_s.reserve(len);
  for (int i = 0; i < len; ++i) {
    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  }
  return tmp_s;
}

}  // namespace

// Create a connection using the test EventEngine to a non-existent listener
// and verify that the connection fails.
TEST_F(EventEngineClientTest, ConnectToNonExistentListenerTest) {
  grpc_core::ExecCtx ctx;
  auto test_ee = this->NewEventEngine();
  Promise<std::unique_ptr<EventEngine::Endpoint>> client_endpoint_promise;
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  // Create a test EventEngine client endpoint and connect to a non existent
  // listener.
  test_ee->Connect(
      [&client_endpoint_promise](
          absl::StatusOr<std::unique_ptr<Endpoint>> status) {
        // Connect should fail.
        EXPECT_FALSE(status.ok());
        client_endpoint_promise.Set(nullptr);
      },
      URIToResolvedAddress("ipv6:[::1]:7000"),
      ChannelArgsEndpointConfig(nullptr),
      memory_quota->CreateMemoryAllocator("conn-1"), 24h);

  auto client_endpoint = std::move(client_endpoint_promise.Get());
  EXPECT_EQ(client_endpoint, nullptr);
}

// Create a connection using the test EventEngine to a listener created
// by the oracle EventEngine and exchange bi-di data over the connection.
// For each data transfer, verify that data written at one end of the stream
// equals data read at the other end of the stream.
TEST_F(EventEngineClientTest, ConnectExchangeBidiDataTransferTest) {
  grpc_core::ExecCtx ctx;
  auto oracle_ee = this->NewOracleEventEngine();
  auto test_ee = this->NewEventEngine();
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  Promise<std::unique_ptr<EventEngine::Endpoint>> client_endpoint_promise;
  Promise<std::unique_ptr<EventEngine::Endpoint>> server_endpoint_promise;

  Listener::AcceptCallback accept_cb =
      [&server_endpoint_promise](
          std::unique_ptr<Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        server_endpoint_promise.Set(std::move(ep));
      };

  auto status = oracle_ee->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) { GPR_ASSERT(status.ok()); },
      ChannelArgsEndpointConfig(nullptr),
      std::make_unique<grpc_core::MemoryQuota>("foo"));
  EXPECT_TRUE(status.ok());

  std::unique_ptr<Listener> listener = std::move(*status);
  EXPECT_TRUE(listener->Bind(URIToResolvedAddress(target_addr)).ok());
  EXPECT_TRUE(listener->Start().ok());

  test_ee->Connect(
      [&client_endpoint_promise](
          absl::StatusOr<std::unique_ptr<Endpoint>> status) {
        if (!status.ok()) {
          gpr_log(GPR_ERROR, "Connect failed: %s",
                  status.status().ToString().c_str());
          client_endpoint_promise.Set(nullptr);
        } else {
          client_endpoint_promise.Set(std::move(*status));
        }
      },
      URIToResolvedAddress(target_addr), ChannelArgsEndpointConfig(nullptr),
      memory_quota->CreateMemoryAllocator("conn-1"), 24h);

  auto client_endpoint = std::move(client_endpoint_promise.Get());
  auto server_endpoint = std::move(server_endpoint_promise.Get());
  EXPECT_TRUE(client_endpoint != nullptr);
  EXPECT_TRUE(server_endpoint != nullptr);

  // Alternate message exchanges between client -- server and server -- client.
  for (int i = 0; i < kNumExchangedMessages; i++) {
    // Send from client to server and verify data read at the server.
    EXPECT_TRUE(SendValidatePayload(GetNextSendMessage(), client_endpoint.get(),
                                    server_endpoint.get())
                    .ok());

    // Send from server to client and verify data read at the client.
    EXPECT_TRUE(SendValidatePayload(GetNextSendMessage(), server_endpoint.get(),
                                    client_endpoint.get())
                    .ok());
  }
}

// Create 1 listener bound to N IPv6 addresses and M connections where M > N and
// exchange and verify random number of messages over each connection.
TEST_F(EventEngineClientTest, MultipleIPv6ConnectionsToOneOracleListenerTest) {
  grpc_core::ExecCtx ctx;
  static constexpr int kNumListenerAddresses = 10;  // N
  static constexpr int kNumConnections = 100;       // M
  auto oracle_ee = this->NewOracleEventEngine();
  auto test_ee = this->NewEventEngine();
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  Promise<std::unique_ptr<EventEngine::Endpoint>> client_endpoint_promise;
  Promise<std::unique_ptr<EventEngine::Endpoint>> server_endpoint_promise;
  std::vector<std::string> target_addrs;
  std::vector<std::tuple<std::unique_ptr<Endpoint>, std::unique_ptr<Endpoint>>>
      connections;

  Listener::AcceptCallback accept_cb =
      [&server_endpoint_promise](
          std::unique_ptr<Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        server_endpoint_promise.Set(std::move(ep));
      };
  auto status = oracle_ee->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) { GPR_ASSERT(status.ok()); },
      ChannelArgsEndpointConfig(nullptr),
      std::make_unique<grpc_core::MemoryQuota>("foo"));
  EXPECT_TRUE(status.ok());
  std::unique_ptr<Listener> listener = std::move(*status);

  target_addrs.reserve(kNumListenerAddresses);
  for (int i = 0; i < kNumListenerAddresses; i++) {
    std::string target_addr = absl::StrCat(
        "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
    EXPECT_TRUE(listener->Bind(URIToResolvedAddress(target_addr)).ok());
    target_addrs.push_back(target_addr);
  }
  EXPECT_TRUE(listener->Start().ok());
  absl::SleepFor(absl::Milliseconds(500));
  for (int i = 0; i < kNumConnections; i++) {
    // Create a test EventEngine client endpoint and connect to a one of the
    // addresses bound to the oracle listener. Verify that the connection
    // succeeds.
    test_ee->Connect(
        [&client_endpoint_promise](
            absl::StatusOr<std::unique_ptr<Endpoint>> status) {
          if (!status.ok()) {
            gpr_log(GPR_ERROR, "Connect failed: %s",
                    status.status().ToString().c_str());
            client_endpoint_promise.Set(nullptr);
          } else {
            client_endpoint_promise.Set(std::move(*status));
          }
        },
        URIToResolvedAddress(target_addrs[i % kNumListenerAddresses]),
        ChannelArgsEndpointConfig(nullptr),
        memory_quota->CreateMemoryAllocator(
            absl::StrCat("conn-", std::to_string(i))),
        24h);

    auto client_endpoint = std::move(client_endpoint_promise.Get());
    auto server_endpoint = std::move(server_endpoint_promise.Get());
    EXPECT_TRUE(client_endpoint != nullptr);
    EXPECT_TRUE(server_endpoint != nullptr);
    connections.push_back(std::make_tuple(std::move(client_endpoint),
                                          std::move(server_endpoint)));
    client_endpoint_promise.Reset();
    server_endpoint_promise.Reset();
  }

  std::vector<std::thread> threads;
  // Create one thread for each connection. For each connection, create
  // 2 more worker threads: to exchange and verify bi-directional data transfer.
  threads.reserve(kNumConnections);
  for (int i = 0; i < kNumConnections; i++) {
    // For each connection, simulate a parallel bi-directional data transfer.
    // All bi-directional transfers are run in parallel across all connections.
    // Each bi-directional data transfer uses a random number of messages.
    threads.emplace_back([client_endpoint =
                              std::move(std::get<0>(connections[i])),
                          server_endpoint =
                              std::move(std::get<1>(connections[i]))]() {
      std::vector<std::thread> workers;
      workers.reserve(2);
      auto worker = [client_endpoint = client_endpoint.get(),
                     server_endpoint =
                         server_endpoint.get()](bool client_to_server) {
        grpc_core::ExecCtx ctx;
        for (int i = 0; i < kNumExchangedMessages; i++) {
          // If client_to_server is true, send from client to server and
          // verify data read at the server. Otherwise send data from server
          // to client and verify data read at client.
          if (client_to_server) {
            EXPECT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                            client_endpoint, server_endpoint)
                            .ok());
          } else {
            EXPECT_TRUE(SendValidatePayload(GetNextSendMessage(),
                                            server_endpoint, client_endpoint)
                            .ok());
          }
        }
      };
      // worker[0] simulates a flow from client to server endpoint
      workers.emplace_back([&worker]() { worker(true); });
      // worker[1] simulates a flow from server to client endpoint
      workers.emplace_back([&worker]() { worker(false); });
      workers[0].join();
      workers[1].join();
    });
  }
  for (auto& t : threads) {
    t.join();
  }
}

// TODO(vigneshbabu): Add more tests which create listeners bound to a mix
// Ipv6 and other type of addresses (UDS) in the same test.
