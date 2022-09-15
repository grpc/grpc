// Copyright 2022 gRPC Authors
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

#include <fcntl.h>
#include <poll.h>

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/promise.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/event_engine/test_suite/event_engine_test_utils.h"
#include "test/core/event_engine/test_suite/oracle_event_engine_posix.h"
#include "test/core/util/port.h"

namespace grpc_event_engine {
namespace posix_engine {

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::PosixEventEngine;
using ::grpc_event_engine::experimental::PosixOracleEventEngine;
using ::grpc_event_engine::experimental::Promise;
using ::grpc_event_engine::experimental::URIToResolvedAddress;
using namespace std::chrono_literals;

namespace {

// Creates a server socket listening for one connection on a specific port. It
// then creates another client socket connected to the server socket. This fills
// up the kernel listen queue on the server socket. Any subsequent attempts to
// connect to the server socket will hang indefinitely. This can be used to
// test Connection timeouts and cancellation attempts.
absl::StatusOr<std::tuple<int, int>> CreateConnectedSocketPair(
    EventEngine::ResolvedAddress resolved_addr) {
  int server_socket;
  int opt = -1;
  int client_socket;
  int one = 1;
  int flags;
  // Creating a new socket file descriptor.
  if ((server_socket = socket(AF_INET6, SOCK_STREAM, 0)) <= 0) {
    return absl::UnknownError(
        absl::StrCat("Error creating socket: ", std::strerror(errno)));
  }
  // MacOS biulds fail if SO_REUSEADDR and SO_REUSEPORT are set in the same
  // setsockopt syscall. So they are set separately one after the other.
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    return absl::UnknownError(
        absl::StrCat("Error setsockopt(SO_REUSEADDR): ", std::strerror(errno)));
  }
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
    return absl::UnknownError(
        absl::StrCat("Error setsockopt(SO_REUSEPORT): ", std::strerror(errno)));
  }

  // Forcefully bind the new socket.
  if (bind(server_socket, resolved_addr.address(), resolved_addr.size()) < 0) {
    return absl::UnknownError(
        absl::StrCat("Error bind: ", std::strerror(errno)));
  }
  // Set the new socket to listen for one active connection at a time.
  if (listen(server_socket, 1) < 0) {
    return absl::UnknownError(
        absl::StrCat("Error listen: ", std::strerror(errno)));
  }
  // Create one client socket and connect it to the server_socket. This ensures
  // that subsequent attempts to connect to the same server will hang
  // indefinitely.
  client_socket = socket(AF_INET6, SOCK_STREAM, 0);
  setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  // Make fd non-blocking.
  flags = fcntl(client_socket, F_GETFL, 0);
  EXPECT_EQ(fcntl(client_socket, F_SETFL, flags | O_NONBLOCK), 0);

  if (connect(client_socket,
              const_cast<struct sockaddr*>(resolved_addr.address()),
              resolved_addr.size()) == -1) {
    if (errno == EINPROGRESS) {
      struct pollfd pfd;
      pfd.fd = client_socket;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      if (poll(&pfd, 1, -1) == -1) {
        gpr_log(GPR_ERROR, "poll() failed during connect; errno=%d", errno);
        abort();
      }
    } else {
      gpr_log(GPR_ERROR, "Failed to connect to the server (errno=%d)", errno);
      abort();
    }
  }
  return std::tuple<int, int>(client_socket, server_socket);
}

}  // namespace

TEST(PosixEventEngineTest, IndefiniteConnectTimeoutTest) {
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  std::string resolved_addr_str =
      SockaddrToString(&resolved_addr, true).value();
  auto sockets = CreateConnectedSocketPair(resolved_addr);
  Promise<bool> connect_promise;
  ASSERT_TRUE(sockets.ok());
  std::shared_ptr<PosixEventEngine> engine =
      std::make_shared<PosixEventEngine>();
  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  ChannelArgsEndpointConfig config(args);
  auto memory_quota = absl::make_unique<grpc_core::MemoryQuota>("bar");
  engine->Connect(
      [&connect_promise, &resolved_addr_str](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> status) {
        ASSERT_FALSE(status.ok());
        absl::Status expected_status = absl::CancelledError(absl::StrCat(
            "Failed to connect to remote host: ", resolved_addr_str,
            " with error: ",
            absl::DeadlineExceededError("connect() timed out").ToString()));
        EXPECT_EQ(status.status(), expected_status);
        connect_promise.Set(true);
      },
      URIToResolvedAddress(target_addr), config,
      memory_quota->CreateMemoryAllocator("conn-1"), 3s);
  ASSERT_TRUE(connect_promise.Get());
  close(std::get<0>(*sockets));
  close(std::get<1>(*sockets));
}

TEST(PosixEventEngineTest, IndefiniteConnectCancellationTest) {
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  std::string resolved_addr_str =
      SockaddrToString(&resolved_addr, true).value();
  auto sockets = CreateConnectedSocketPair(resolved_addr);
  ASSERT_TRUE(sockets.ok());
  std::shared_ptr<PosixEventEngine> engine =
      std::make_shared<PosixEventEngine>();
  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  ChannelArgsEndpointConfig config(args);
  auto memory_quota = absl::make_unique<grpc_core::MemoryQuota>("bar");
  auto connection_handle = engine->Connect(
      [](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> /*status*/) {
        ASSERT_FALSE(
            "The on_connect callback should not have run since the connection "
            "attempt was cancelled.");
      },
      URIToResolvedAddress(target_addr), config,
      memory_quota->CreateMemoryAllocator("conn-2"), 3s);
  ASSERT_TRUE(engine->CancelConnect(connection_handle));
  close(std::get<0>(*sockets));
  close(std::get<1>(*sockets));
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
