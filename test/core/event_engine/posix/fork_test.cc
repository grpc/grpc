// Copyright 2025 gRPC Authors
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

#include <arpa/inet.h>
#include <fcntl.h>
#include <gmock/gmock.h>
#include <grpc/event_engine/event_engine.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/helloworld.pb.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
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

MATCHER(IsOkStatus, "Is ok") {
  if ((!arg.ok())) {
    *result_listener << arg.error_message();
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

struct ClientAndServer {
  FileDescriptor client;
  FileDescriptor server;
};

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
  FileDescriptors system_api;
  std::unique_ptr<TestScheduler> scheduler;
};

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

using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

grpc::Status CallSayHello(const std::unique_ptr<Greeter::Stub>& stub) {
  grpc::ClientContext context;
  HelloRequest request;
  request.set_name("system_api_test");
  HelloReply response;
  return stub->SayHello(&context, request, &response);
}

grpc_core::ChannelArgs BuildChannelArgs() {
  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  return args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
}

class OutOfProcessServer {
 public:
  OutOfProcessServer(pid_t pid, int port) : pid_(pid), port_(port) {}
  OutOfProcessServer(const OutOfProcessServer& /* other */) = delete;
  OutOfProcessServer(OutOfProcessServer&& other) noexcept
      : pid_(other.pid_), port_(other.port_) {
    other.pid_ = 0;
  }
  ~OutOfProcessServer() {
    if (pid_ != 0) {
      int stat;
      kill(pid_, SIGTERM);
      waitpid(pid_, &stat, 0);
      LOG(INFO) << stat;
    }
  }

  std::string url() const { return absl::StrCat("localhost:", port_); }

 private:
  pid_t pid_;
  int port_;
};

absl::StatusOr<OutOfProcessServer> StartServerGetPort() {
  std::array<int, 2> pipe_fds;
  if (pipe(pipe_fds.data()) != 0) {
    return absl::ErrnoToStatus(errno, "Creating pipe");
  }
  pid_t pid = fork();
  if (pid < 0) {
    return absl::ErrnoToStatus(errno, "Fork");
  } else if (pid == 0) {
    int port = grpc_pick_unused_port_or_die();
    close(pipe_fds[0]);
    if (write(pipe_fds[1], &port, sizeof(port)) < 0) {
      std::cerr << absl::ErrnoToStatus(errno, "Writing port") << "\n";
      exit(1);
    }
    close(pipe_fds[1]);
    absl::Status server_status = ExecServer(port);
    if (!server_status.ok()) {
      std::cerr << server_status << "\n";
      exit(1);
    }
    exit(0);
  }
  close(pipe_fds[1]);
  int port;
  int r = 0;
  while (r < sizeof(port)) {
    int rd = read(pipe_fds[0], &port + r, sizeof(port) - r);
    if (rd < 0) {
      return absl::ErrnoToStatus(errno, "Reading the pipe");
    }
    r += rd;
  }
  close(pipe_fds[0]);
  return OutOfProcessServer(pid, port);
}

}  // namespace

TEST(PosixSystemApiTest, ChildFork) {
  absl::StatusOr<OutOfProcessServer> server = StartServerGetPort();
  ASSERT_TRUE(server.ok()) << server.status();
  // Give the child time to start up
  absl::SleepFor(absl::Milliseconds(1000));
  // First call - it works.
  auto channel =
      grpc::CreateChannel(server->url(), grpc::InsecureChannelCredentials());
  auto stub = Greeter::NewStub(channel);
  EXPECT_THAT(CallSayHello(stub), IsOkStatus());
  // Simulating fork
  auto ee = GetDefaultEventEngine();
  LOG(INFO) << "EventEngine: " << ee.get() << " pid: " << getpid();
  // PosixEventEngine* posix_ee = static_cast<PosixEventEngine*>(ee.get());
  // ASSERT_THAT(posix_ee->HandlePreFork(), IsOk());
  // ASSERT_THAT(posix_ee->HandleForkInChild(), IsOk());
  // EXPECT_THAT(CallSayHello(stub), ::testing::Not(IsOkStatus()));
  // EXPECT_THAT(CallSayHello(stub), IsOkStatus());
  // // This call hangs (but will be fixed)
  // auto channel2 =
  //     grpc::CreateChannel(server->url(), grpc::InsecureChannelCredentials());
  // EXPECT_THAT(CallSayHello(Greeter::NewStub(channel)), IsOkStatus());
}

TEST(PosixSystemApiTest, ParentFork) {
  absl::StatusOr<OutOfProcessServer> server_process = StartServerGetPort();
  ASSERT_TRUE(server_process.ok()) << server_process.status();
  LOG(INFO) << "Parent pid: " << getpid() << " url: " << server_process->url();
  absl::SleepFor(absl::Milliseconds(200));
  // Simulating fork
  grpc_init();
  auto ee = GetDefaultEventEngine();
  ASSERT_NE(ee, nullptr);
  // PosixEventEngine* posix_ee = static_cast<PosixEventEngine*>(ee.get());
  // ASSERT_THAT(posix_ee->HandlePreFork(), IsOk());
  // ASSERT_THAT(posix_ee->HandleFork(), IsOk());
  // // This call hangs (but will be fixed)
  // auto channel = grpc::CreateChannel(server_process->url(),
  //                                    grpc::InsecureChannelCredentials());
  // auto stub = Greeter::NewStub(channel);
  // EXPECT_THAT(CallSayHello(stub), IsOkStatus());
}

TEST(ForkTest, ListenerOnFork) {
  int port = grpc_pick_unused_port_or_die();
  auto ee = GetDefaultEventEngine();
  TestScheduler scheduler(ee.get());
  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  ChannelArgsEndpointConfig config(args);
  absl::Mutex mu;
  std::optional<absl::Status> listener_done;
  std::vector<std::unique_ptr<EventEngine::Endpoint>> endpoints;
  auto listener = ee->CreateListener(
      [&](auto endpoint, MemoryAllocator /* memory */) {
        LOG(INFO) << "connection! "
                  << ResolvedAddressToURI(endpoint->GetPeerAddress());
        absl::MutexLock lock(&mu);
        endpoints.emplace_back(std::move(endpoint));
      },
      [&](absl::Status status) {
        absl::MutexLock lock(&mu);
        listener_done.emplace(std::move(status));
      },
      config, std::make_unique<grpc_core::MemoryQuota>("foo"));
  ASSERT_TRUE(listener.ok()) << listener.status();
  auto address = URIToResolvedAddress(absl::StrCat("ipv4:127.0.0.1:", port));
  ASSERT_TRUE(address.ok()) << address.status();
  auto bound_port = (*listener)->Bind(*address);
  ASSERT_TRUE(bound_port.ok()) << bound_port;
  LOG(INFO) << *bound_port;
  auto status = (*listener)->Start();
  ASSERT_TRUE(status.ok()) << status;

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(sockfd, 0) << absl::ErrnoToStatus(errno, "Creating socket");

  int result = connect(sockfd, address->address(), sizeof(*address->address()));
  EXPECT_GE(result, 0) << absl::ErrnoToStatus(errno, "Connect");

  {
    absl::MutexLock lock(&mu);
    mu.Await(
        {+[](std::vector<std::unique_ptr<EventEngine::Endpoint>>* endpoints) {
           return !endpoints->empty();
         },
         &endpoints});
  }

  listener->reset();
  absl::Condition cond(
      +[](std::optional<absl::Status>* opt) { return opt->has_value(); },
      &listener_done);
  {
    absl::MutexLock lock(&mu);
    mu.Await(cond);
  }
  LOG(INFO) << *listener_done;
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
