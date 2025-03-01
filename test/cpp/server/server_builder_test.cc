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

#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/config.h>
#include <sys/socket.h>

#include "gtest/gtest.h"
#include "src/core/util/notification.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

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
  auto server =
      ServerBuilder()
          .experimental()
          .AddPassiveListener(InsecureServerCredentials(), passive_listener)
          .BuildAndStart();
  server->Shutdown();
}

TEST_F(ServerBuilderTest, PassiveListenerAcceptConnectedFd) {
  std::unique_ptr<experimental::PassiveListener> passive_listener;
  ServerBuilder builder;
  auto cq = builder.AddCompletionQueue();
  // TODO(hork): why is the service necessary? Queue isn't drained otherwise.
  auto server =
      builder.RegisterService(&g_service)
          .experimental()
          .AddPassiveListener(InsecureServerCredentials(), passive_listener)
          .BuildAndStart();
  ASSERT_NE(server.get(), nullptr);
#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  auto accept_status = passive_listener->AcceptConnectedFd(fd);
  ASSERT_TRUE(accept_status.ok()) << accept_status;
#else
  int fd = -1;
  auto accept_status = passive_listener->AcceptConnectedFd(fd);
  ASSERT_FALSE(accept_status.ok()) << accept_status;
#endif
  server->Shutdown();
}

TEST_F(ServerBuilderTest, PassiveListenerAcceptConnectedEndpoint) {
  std::unique_ptr<experimental::PassiveListener> passive_listener;
  auto server =
      ServerBuilder()
          .experimental()
          .AddPassiveListener(InsecureServerCredentials(), passive_listener)
          .BuildAndStart();
  grpc_core::Notification endpoint_destroyed;
  auto success = passive_listener->AcceptConnectedEndpoint(
      std::make_unique<grpc_event_engine::experimental::ThreadedNoopEndpoint>(
          &endpoint_destroyed));
  ASSERT_TRUE(success.ok())
      << "AcceptConnectedEndpoint failure: " << success.ToString();
  endpoint_destroyed.WaitForNotification();
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
