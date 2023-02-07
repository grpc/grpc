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

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/config.h>

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

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
