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
#include "absl/time/time.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"
#include "test/core/event_engine/test_suite/event_engine_test_utils.h"

class EventEngineClientTest : public EventEngineTest {};

namespace {

using ::grpc_event_engine::experimental::ConnectionManager;
using ResolvedAddress =
    ::grpc_event_engine::experimental::EventEngine::ResolvedAddress;
using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;

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

// Create a connection using the test event engine to a non-existent listener
// and verify that the connection fails.
TEST_F(EventEngineClientTest, ConnectToNonExistentListenerTest) {
  grpc_core::ExecCtx ctx;
  ConnectionManager mgr(this->NewEventEngine(), this->NewOracleEventEngine());
  // Create a test event engine client endpoint and connect to a non existent
  // oracle listener.
  auto status =
      mgr.CreateConnection("ipv6:[::1]:7000", absl::InfiniteFuture(), false);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status(),
            absl::CancelledError("Failed to create connection."));
}

// Create a connection using the test event engine to a listener created
// by the oracle event engine and exchange bi-di data over the connection.
// For each data transfer, verify that data written at one end of the stream
// equals data read at the other end of the stream.
TEST_F(EventEngineClientTest, ConnectExchangeBidiDataTransferTest) {
  grpc_core::ExecCtx ctx;
  ConnectionManager mgr(this->NewEventEngine(), this->NewOracleEventEngine());
  std::string target_addr = "ipv6:[::1]:7000";
  // Start an oracle localhost ipv6 listener bound to 1 address.
  GPR_ASSERT(mgr.BindAndStartListener({target_addr}).ok());
  // Create a test event engine client endpoint and connect to oracle listener.
  auto status =
      mgr.CreateConnection(target_addr, absl::InfiniteFuture(), false);
  EXPECT_TRUE(status.ok());
  auto client_endpoint = std::move(std::get<0>(*status));
  auto server_endpoint = std::move(std::get<1>(*status));
  // Alternate message exchanges between client -- server and server -- client.
  for (int i = 0; i < kNumExchangedMessages; i++) {
    // Send from client to server and verify data read at the server.
    EXPECT_TRUE(ExchangeVerifyData(GetNextSendMessage(), client_endpoint.get(),
                                   server_endpoint.get())
                    .ok());

    // Send from server to client and verify data read at the client.
    EXPECT_TRUE(ExchangeVerifyData(GetNextSendMessage(), server_endpoint.get(),
                                   client_endpoint.get())
                    .ok());
  }
}

// Create 1 listener bound to N IPv6 addresses and M connections where M > N and
// exchange and verify random number of messages over each connection.
TEST_F(EventEngineClientTest, MultipleIPv6ConnectionsToOneOracleListenerTest) {
  grpc_core::ExecCtx ctx;
  ConnectionManager mgr(this->NewEventEngine(), this->NewOracleEventEngine());
  static constexpr int kStartPortNumber = 7000;
  static constexpr int kNumListenerAddresses = 10;  // N
  static constexpr int kNumConnections = 100;       // M
  std::vector<std::string> target_addrs;
  std::vector<std::tuple<std::unique_ptr<Endpoint>, std::unique_ptr<Endpoint>>>
      connections;
  for (int i = 0; i < kNumListenerAddresses; i++) {
    target_addrs.push_back(
        absl::StrCat("ipv6:[::1]:", std::to_string(kStartPortNumber + i)));
  }
  // Create 1 oracle listener bound to 10 ipv6 addresses.
  EXPECT_TRUE(mgr.BindAndStartListener(target_addrs).ok());
  absl::SleepFor(absl::Milliseconds(500));
  for (int i = 0; i < kNumConnections; i++) {
    // Create a test event engine client endpoint and connect to a one of the
    // addresses bound to the oracle listener. Verify that the connection
    // succeeds.
    auto status = mgr.CreateConnection(target_addrs[i % kNumListenerAddresses],
                                       absl::InfiniteFuture(), false);
    EXPECT_TRUE(status.ok());
    connections.push_back(std::move(*status));
  }
  std::vector<std::thread> threads;
  // Create one thread for each connection. For each connection, create
  // 2 more worker threads: to exchange and verify bi-directional data transfer.
  threads.reserve(kNumConnections);
  for (int i = 0; i < kNumConnections; i++) {
    // For each connection, simulate a parallel bi-directional data transfer.
    // All bi-directional transfers are run in parallel across all connections.
    // Each bi-directional data transfer uses a random number of messages.
    threads.emplace_back(
        [client_endpoint = std::move(std::get<0>(connections[i])),
         server_endpoint = std::move(std::get<1>(connections[i]))]() {
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
                EXPECT_TRUE(ExchangeVerifyData(GetNextSendMessage(),
                                               client_endpoint, server_endpoint)
                                .ok());
              } else {
                EXPECT_TRUE(ExchangeVerifyData(GetNextSendMessage(),
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
