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
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"
#include "test/core/event_engine/test_suite/event_engine_test_utils.h"

class EventEngineClientTest : public EventEngineTest {};

using grpc_event_engine::experimental::ConnectionManager;
using ResolvedAddress =
    grpc_event_engine::experimental::EventEngine::ResolvedAddress;

static constexpr int kMinMessageSize = 1024;
static constexpr int kMaxMessageSize = 4096;
static constexpr int kNumExchangedMessages = 100;

namespace {

grpc_core::Mutex g_mu;

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

// TODO(hork): establish meaningful tests

// Create a connection using the test event engine to a non-existent listener
// and verify that the connection fails.
TEST_F(EventEngineClientTest, ConnectToNonExistentListenerTest) {
  ConnectionManager mgr(this->NewEventEngine(), this->NewOracleEventEngine());
  // Create a test event engine client endpoint and connect to a non existent
  // oracle listener.
  auto status = mgr.CreateConnection(
      "ipv6:[::1]:7000", absl::InfiniteFuture(),
      [](absl::Status status) {
        // The On-Connect callback will be called with the status reported by
        // the test event engine.
        GPR_ASSERT(!status.ok());
      },
      false);
  GPR_ASSERT(status.status() ==
             absl::CancelledError("Failed to create connection."));
}

// Create a connection using the test event engine to a listener created
// by the oracle event engine and exchange bi-di data over the connection.
// For each data transfer, verify that data written at one end of the stream
// equals data read at the other end of the stream.
TEST_F(EventEngineClientTest, ConnectExchangeBidiDataTransferTest) {
  ConnectionManager mgr(this->NewEventEngine(), this->NewOracleEventEngine());
  std::string target_addr = "ipv6:[::1]:7000";
  // Start an oracle localhost ipv6 listener
  GPR_ASSERT(
      mgr.StartListener(target_addr, /*listener_type_oracle=*/true).ok());
  // Create a test event engine client endpoint and connect to oracle listener.
  auto status = mgr.CreateConnection(
      target_addr, absl::InfiniteFuture(),
      [](absl::Status status) { GPR_ASSERT(status.ok()); }, false);
  GPR_ASSERT(status.ok());
  int connection_id = *status;
  // Alternate message exchanges between client -- server and server -- client.
  for (int i = 0; i < kNumExchangedMessages; i++) {
    // Send from client to server and verify data read at the server.
    GPR_ASSERT(mgr.TransferFromClient(/*connection_id=*/connection_id,
                                      /*write_data=*/GetNextSendMessage())
                   .ok());

    // Send from server to client and verify data read at the client.
    GPR_ASSERT(mgr.TransferFromServer(/*connection_id=*/connection_id,
                                      /*write_data=*/GetNextSendMessage())
                   .ok());
  }
  mgr.CloseConnection(connection_id);
}

// Create a N listeners and M connections where M > N and exchange and verify
// data over each connection.
TEST_F(EventEngineClientTest, MatrixOfConnectionsToOracleListenersTest) {
  ConnectionManager mgr(this->NewEventEngine(), this->NewOracleEventEngine());
  static constexpr int kStartPortNumber = 7000;
  static constexpr int kNumListeners = 10;
  static constexpr int kNumConnections = 100;
  static constexpr int kMinMessagesInConnection = 10;
  static constexpr int kMaxMessagesInConnection = 100;
  std::vector<std::string> target_addrs;
  std::vector<int> connections;
  for (int i = 0; i < kNumListeners; i++) {
    std::string target_addr =
        absl::StrCat("ipv6:[::1]:", std::to_string(kStartPortNumber + i));
    // Start an oracle localhost ipv6 listener
    GPR_ASSERT(mgr.StartListener(target_addr, true).ok());
    target_addrs.push_back(target_addr);
  }
  absl::SleepFor(absl::Milliseconds(500));
  for (int i = 0; i < kNumConnections; i++) {
    // Create a test event engine client endpoint and connect to a random oracle
    // listener. Verify that the connection succeeds.
    auto status = mgr.CreateConnection(
        target_addrs[rand() % kNumListeners], absl::InfiniteFuture(),
        [](absl::Status status) { GPR_ASSERT(status.ok()); }, false);
    GPR_ASSERT(status.ok());
    connections.push_back(*status);
  }
  std::vector<std::thread> threads;
  threads.reserve(kNumConnections);
  for (int i = 0; i < kNumConnections; i++) {
    // For each connection, simulate a parallel bi-directional data transfer.
    // All bi-directional transfers are run in parallel across all connections.
    // Each bi-directional data transfer uses a random number of messages.
    threads.emplace_back([&, connection_id = connections[i]]() {
      std::random_device rd;
      std::mt19937 gen(rd());
      // Randomize the number of messages per connection to randomize
      // its lifetime.
      std::uniform_real_distribution<> dis(kMinMessagesInConnection,
                                           kMaxMessagesInConnection);
      int kNumExchangedMessages = dis(gen);
      std::vector<std::thread> workers;
      workers.reserve(2);
      auto worker = [&mgr, connection_id,
                     kNumExchangedMessages](bool client_to_server) {
        for (int i = 0; i < kNumExchangedMessages; i++) {
          // If client_to_server is true, send from client to server and verify
          // data read at the server. Otherwise send data from server to client
          // and verify data read at client.
          if (client_to_server) {
            GPR_ASSERT(
                mgr.TransferFromClient(/*connection_id=*/connection_id,
                                       /*write_data=*/GetNextSendMessage())
                    .ok());
          } else {
            GPR_ASSERT(
                mgr.TransferFromServer(/*connection_id=*/connection_id,
                                       /*write_data=*/GetNextSendMessage())
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
      mgr.CloseConnection(connection_id);
    });
  }
  for (auto& t : threads) {
    t.join();
  }
}
