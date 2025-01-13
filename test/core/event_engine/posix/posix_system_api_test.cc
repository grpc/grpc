// Copyright 2024 gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/posix_system_api.h"

#include <arpa/inet.h>
#include <gmock/gmock.h>
#include <grpc/event_engine/event_engine.h>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/helloworld.pb.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "test/core/event_engine/posix/posix_engine_test_utils.h"
#include "test/core/test_util/port.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

MATCHER(FDReady, "file descriptor is ready") { return ((arg.ready())); }

MATCHER_P(IsOkWith, value, "") {
  if ((arg.ok())) {
    return testing::ExplainMatchResult(value, arg.value(), result_listener);
  } else {
    *result_listener << arg.status();
    return false;
  }
}

MATCHER(IsOk, "Is ok") {
  if ((!arg.ok())) {
    *result_listener << arg;
    return false;
  }
  return true;
}

sockaddr_in SockAddr(int port) {
  sockaddr_in addr{0};
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;
  return addr;
}

absl::StatusOr<FileDescriptor> Listen(SystemApi& system_api, int port) {
  FileDescriptor server = system_api.Socket(AF_INET, SOCK_STREAM, 0);
  if (!server.ready()) {
    return absl::ErrnoToStatus(errno, "Failed to create a socket");
  }
  auto result = system_api.SetNonBlocking(server, true);
  if (!result.ok()) {
    return result;
  }
  sockaddr_in addr = SockAddr(port);
  sockaddr* sa = reinterpret_cast<sockaddr*>(&addr);
  if (system_api.Bind(server, sa, sizeof(addr)).value_or(-1) < 0) {
    return absl::ErrnoToStatus(errno, "Bind to an address failed");
  }
  if (system_api.Listen(server, 1).value_or(-1) < 0) {
    return absl::ErrnoToStatus(errno, "Listen failed");
  };
  return server;
}

struct ClientAndServer {
  FileDescriptor client;
  FileDescriptor server;
};

absl::StatusOr<ClientAndServer> EstablishConnection(
    SystemApi& server_system_api, SystemApi& client_system_api,
    const FileDescriptor& server, int port) {
  FileDescriptor client = client_system_api.Socket(AF_INET, SOCK_STREAM, 0);
  if (!client.ready()) {
    return absl::ErrnoToStatus(errno, "Unable to create a client socket");
  }
  auto nb_result = client_system_api.SetNonBlocking(client, true);
  if (!nb_result.ok()) {
    return nb_result;
  }
  sockaddr_in addr = SockAddr(port);
  auto result = client_system_api.Connect(
      client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (!result.ok() || *result == 0 || errno != EINPROGRESS) {
    return absl::ErrnoToStatus(errno, "Connect is not EINPROGRESS");
  }
  // auto server_end = Accept(server_system_api, server);
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  absl::StatusOr<FileDescriptor> server_end =
      server_system_api.Accept(server, reinterpret_cast<sockaddr*>(&ss), &slen);
  if (!server_end.ok() || !server_end->ready()) {
    return absl::ErrnoToStatus(errno, "Accept failed");
  }
  result = client_system_api.Connect(client, reinterpret_cast<sockaddr*>(&addr),
                                     sizeof(addr));
  if (!result.ok() || *result < 0) {
    return absl::ErrnoToStatus(errno, "Second connect failed");
  }
  return ClientAndServer{client, *server_end};
}

class EventEngineForTest {
 public:
  EventEngineForTest()
      : system_api(), scheduler(std::make_unique<TestScheduler>()) {
    poller = MakeDefaultPoller(scheduler.get());
    if (poller != nullptr) {
      event_engine = PosixEventEngine::MakeTestOnlyPosixEventEngine(poller);
      scheduler->ChangeCurrentEventEngine(event_engine.get());
    }
  }

  bool ok() const { return poller != nullptr; }

  std::shared_ptr<PosixEventEngine> event_engine;
  std::shared_ptr<PosixEventPoller> poller;
  SystemApi system_api;
  std::unique_ptr<TestScheduler> scheduler;
};

grpc_core::ChannelArgs BuildChannelArgs() {
  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  return args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
}

}  // namespace

TEST(PosixSystemApiTest, PosixLevel) {
  SystemApi server_api;
  SystemApi client_api;

  int port = grpc_pick_unused_port_or_die();

  auto server = Listen(server_api, port);
  ASSERT_THAT(server, IsOkWith(FDReady()));
  auto server_client =
      EstablishConnection(server_api, client_api, *server, port);
  ASSERT_TRUE(server_client.ok()) << server_client.status();
  // Send from client to server
  std::array<uint8_t, 3> buf = {0x20, 0x30, 0x30};
  ASSERT_THAT(client_api.Write(server_client->client, buf.data(), buf.size()),
              IsOkWith(buf.size()));
  std::array<uint8_t, 20> rcv;
  rcv.fill(0);
  ASSERT_THAT(server_api.Read(server_client->server, rcv.data(), rcv.size()),
              IsOkWith(buf.size()));
  EXPECT_THAT(absl::MakeSpan(rcv).first(buf.size()),
              ::testing::ElementsAreArray(buf));
  // Client "forks"
  EXPECT_THAT(client_api.AdvanceGeneration(), IsOk());
  ASSERT_EQ(client_api.Write(server_client->client, buf.data(), buf.size())
                .status()
                .code(),
            absl::StatusCode::kInternal);
  // Send using the new connection
  server_client = EstablishConnection(server_api, client_api, *server, port);
  ASSERT_TRUE(server_client.ok()) << server_client.status();
  ASSERT_THAT(client_api.Write(server_client->client, buf.data(), buf.size()),
              IsOkWith(buf.size()));
  // Make sure previous run does not leak here
  rcv.fill(0);
  ASSERT_THAT(server_api.Read(server_client->server, rcv.data(), rcv.size()),
              IsOkWith(buf.size()));
  EXPECT_THAT(absl::MakeSpan(rcv).first(buf.size()),
              ::testing::ElementsAreArray(buf));
}

TEST(PosixSystemApiTest, DISABLED_IncompleteEventEndpointLevel) {
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto address = URIToResolvedAddress(target_addr);
  ASSERT_TRUE(address.ok()) << address.status();
  EventEngineForTest ee_server;
  EventEngineForTest ee_client;
  ASSERT_TRUE(ee_client.ok());
  ASSERT_TRUE(ee_server.ok());

  std::unique_ptr<EventEngine::Endpoint> server_end;
  grpc_core::Mutex mu;
  grpc_core::CondVar cond;
  EventEngine::Listener::AcceptCallback accept_cb =
      [&](std::unique_ptr<EventEngine::Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        grpc_core::MutexLock lock(&mu);
        CHECK_EQ(server_end.get(), nullptr)
            << "Previous endpoint was not claimed";
        server_end = std::move(ep);
        cond.SignalAll();
      };
  ChannelArgsEndpointConfig config(BuildChannelArgs());
  absl::optional<absl::Status> listener_shutdown_status;
  auto listener = ee_server.event_engine->CreateListener(
      std::move(accept_cb),
      [&](const absl::Status& status) {
        grpc_core::MutexLock lock(&mu);
        LOG(INFO) << "Boop!";
        listener_shutdown_status = status;
        cond.SignalAll();
      },
      config, std::make_unique<grpc_core::MemoryQuota>("foo"));
  EXPECT_THAT(listener, IsOkWith(::testing::Ne(nullptr)));
  absl::Status status = listener.value()->Bind(*address).status();
  EXPECT_THAT(status, IsOk());
  status = listener.value()->Start();
  EXPECT_THAT(status, IsOk());
  grpc_core::MemoryQuota quota("client");

  absl::optional<absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>>
      client_end;

  ee_client.event_engine->Connect(
      [&](auto conn) {
        grpc_core::MutexLock lock(&mu);
        client_end = std::move(conn);
        cond.SignalAll();
      },
      *address, config, quota.CreateMemoryAllocator("first connection"),
      grpc_core::Duration::Seconds(5));
  {
    grpc_core::MutexLock lock(&mu);
    while (!listener_shutdown_status.has_value() || !client_end.has_value()) {
      LOG(INFO) << listener_shutdown_status.has_value() << " "
                << client_end.has_value();
      ee_server.poller->Work(EventEngine::Duration(10),
                             []() { LOG(INFO) << "Server!"; });
      ee_client.poller->Work(EventEngine::Duration(10),
                             []() { LOG(INFO) << "Client!"; });
      cond.Wait(&mu);
    }
    // EXPECT_THAT(*listener_shutdown_status, IsOk());
  }

  // worker_ = new Worker(event_engine_, poller_.get());
  // worker_->Start();
}

namespace {

absl::Status ExecServer(int port) {
  char kExecutable[] = "examples/cpp/helloworld/greeter_server";
  std::string port_arg = absl::StrCat("--port=", port);
  std::vector<char> v = {port_arg.begin(), port_arg.end()};
  v.push_back('\0');
  char* const args[] = {kExecutable, v.data(), nullptr};
  if (execve(kExecutable, args, nullptr) < 0) {
    return absl::ErrnoToStatus(errno, "execve");
  } else {
    // Should never happen
    return absl::OkStatus();
  }
}

void ShutdownChild(int pid) {
  int stat;
  kill(pid, SIGTERM);
  waitpid(pid, &stat, 0);
  LOG(INFO) << stat;
}

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

}  // namespace

TEST(PosixSystemApiTest, FullGrpc) {
  int port = grpc_pick_unused_port_or_die();
  int pid = fork();
  ASSERT_GE(pid, 0) << absl::ErrnoToStatus(errno, "Fork");
  if (pid == 0) {
    ASSERT_THAT(ExecServer(port), IsOk());
  }
  auto cleanup = absl::MakeCleanup([pid]() { ShutdownChild(pid); });
  // Give the child time to start up
  absl::SleepFor(absl::Milliseconds(1000));
  std::string target = absl::StrCat("localhost:", port);
  auto channel =
      grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  auto stub = Greeter::NewStub(channel);
  HelloRequest request;
  request.set_name("system_api_test");

  grpc::ClientContext context;
  HelloReply response;
  auto status = stub->SayHello(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "Hello system_api_test");
  auto ee = GetDefaultEventEngine();
  PosixEventEngine* posix_ee = static_cast<PosixEventEngine*>(ee.get());
  ASSERT_THAT(posix_ee->HandlePreFork(), IsOk());
  ASSERT_THAT(posix_ee->HandleForkInChild(), IsOk());
  request.set_name("trying after shutdown");
  grpc::ClientContext ctx2;
  response = {};
  status = stub->SayHello(&ctx2, request, &response);
  EXPECT_TRUE(status.ok()) << absl::StrFormat("(%d) %s", status.error_code(),
                                              status.error_message());
  EXPECT_EQ(response.message(), "Hello system_api_test");
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
