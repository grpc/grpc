//
//
// Copyright 2017 gRPC authors.
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
//
//

#include <sys/socket.h>

#include <thread>

#include <gtest/gtest.h>

#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/config.h>

#include "src/core/lib/gprpp/notification.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace {

testing::EchoTestService::Service g_service;

std::string MakePort() {
  std::ostringstream s;
  int p = grpc_pick_unused_port_or_die();
  s << "localhost:" << p;
  return s.str();
}

const std::string& GetPort() {
  static std::string g_port = MakePort();
  return g_port;
}

class ServerBuilderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { grpc_init(); }

  static void TearDownTestSuite() { grpc_shutdown(); }
};
TEST_F(ServerBuilderTest, NoOp) { ServerBuilder b; }

TEST_F(ServerBuilderTest, CreateServerNoPorts) {
  ServerBuilder().RegisterService(&g_service).BuildAndStart()->Shutdown();
}

TEST_F(ServerBuilderTest, CreateServerOnePort) {
  ServerBuilder()
      .RegisterService(&g_service)
      .AddListeningPort(GetPort(), InsecureServerCredentials())
      .BuildAndStart()
      ->Shutdown();
}

TEST_F(ServerBuilderTest, CreateServerRepeatedPort) {
  ServerBuilder()
      .RegisterService(&g_service)
      .AddListeningPort(GetPort(), InsecureServerCredentials())
      .AddListeningPort(GetPort(), InsecureServerCredentials())
      .BuildAndStart()
      ->Shutdown();
}

TEST_F(ServerBuilderTest, CreateServerRepeatedPortWithDisallowedReusePort) {
  EXPECT_EQ(ServerBuilder()
                .RegisterService(&g_service)
                .AddListeningPort(GetPort(), InsecureServerCredentials())
                .AddListeningPort(GetPort(), InsecureServerCredentials())
                .AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0)
                .BuildAndStart(),
            nullptr);
}

TEST_F(ServerBuilderTest, AddPassiveListener) {
  std::unique_ptr<experimental::PassiveListener> passive_listener;
  ServerBuilder()
      .AddPassiveListener(InsecureServerCredentials(), passive_listener)
      .BuildAndStart();
}

TEST_F(ServerBuilderTest, PassiveListenerAcceptConnectedFd) {
#ifndef GPR_SUPPORT_CHANNELS_FROM_FD
  GTEST_SKIP() << "Platform does not support fds";
#endif
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  std::unique_ptr<experimental::PassiveListener> passive_listener;
  ServerBuilder builder;
  auto cq = builder.AddCompletionQueue();
  // TODO(hork): why is the service necessary? Queue isn't drained otherwise.
  auto server =
      builder.RegisterService(&g_service)
          .AddPassiveListener(InsecureServerCredentials(), passive_listener)
          .BuildAndStart();
  ASSERT_NE(server.get(), nullptr);
  auto accept_status = passive_listener->AcceptConnectedFd(fd);
#ifndef GPR_SUPPORT_CHANNELS_FROM_FD
  ASSERT_FALSE(accept_status.ok())
      << "fds are not supported on this platform, this should have failed";
#endif
  ASSERT_TRUE(accept_status.ok()) << accept_status;
  server->Shutdown();
}

TEST_F(ServerBuilderTest, PassiveListenerAcceptConnectedEndpoint) {
  class NoopEndpoint
      : public grpc_event_engine::experimental::EventEngine::Endpoint {
   public:
    NoopEndpoint(std::thread** read_thread, std::thread** write_thread,
                 grpc_core::Notification* destroyed)
        : read_thread_(read_thread),
          write_thread_(write_thread),
          destroyed_(destroyed) {}
    ~NoopEndpoint() override { destroyed_->Notify(); }
    bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_event_engine::experimental::SliceBuffer* /* buffer */,
              const ReadArgs* /* args */) override {
      if (*read_thread_ != nullptr) (*read_thread_)->join();
      *read_thread_ = new std::thread([on_read = std::move(on_read)]() mutable {
        on_read(absl::UnknownError("test"));
      });
      return false;
    }
    bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
               grpc_event_engine::experimental::SliceBuffer* /* data */,
               const WriteArgs* /* args */) override {
      if (*write_thread_ != nullptr) (*write_thread_)->join();
      *write_thread_ =
          new std::thread([on_writable = std::move(on_writable)]() mutable {
            on_writable(absl::UnknownError("test"));
          });
      return false;
    }
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
    GetPeerAddress() const override {
      return peer_;
    }
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
    GetLocalAddress() const override {
      return local_;
    }

   private:
    grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_;
    grpc_event_engine::experimental::EventEngine::ResolvedAddress local_;
    std::thread** read_thread_;
    std::thread** write_thread_;
    grpc_core::Notification* destroyed_;
  };
  std::unique_ptr<experimental::PassiveListener> passive_listener;
  auto server =
      ServerBuilder()
          .AddPassiveListener(InsecureServerCredentials(), passive_listener)
          .BuildAndStart();
  std::thread* read_thread = nullptr;
  std::thread* write_thread = nullptr;
  grpc_core::Notification endpoint_destroyed;
  passive_listener->AcceptConnectedEndpoint(std::make_unique<NoopEndpoint>(
      &read_thread, &write_thread, &endpoint_destroyed));
  // The passive listener holds a server ref, so it must be destroyed before the
  // server can shut down
  endpoint_destroyed.WaitForNotification();
  if (read_thread != nullptr) read_thread->join();
  if (write_thread != nullptr) write_thread->join();
  server->Shutdown();
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
