/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc++/impl/codegen/config.h>
#include <gtest/gtest.h>

#include <grpc++/server.h>
#include <grpc++/server_builder.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"

namespace grpc {
namespace {

testing::EchoTestService::Service g_service;

grpc::string MakePort() {
  std::ostringstream s;
  int p = grpc_pick_unused_port_or_die();
  s << "localhost:" << p;
  return s.str();
}

grpc::string g_port = MakePort();

TEST(ServerBuilderTest, NoOp) { ServerBuilder b; }

TEST(ServerBuilderTest, CreateServerNoPorts) {
  ServerBuilder().RegisterService(&g_service).BuildAndStart()->Shutdown();
}

TEST(ServerBuilderTest, CreateServerOnePort) {
  ServerBuilder()
      .RegisterService(&g_service)
      .AddListeningPort(g_port, InsecureServerCredentials())
      .BuildAndStart()
      ->Shutdown();
}

TEST(ServerBuilderTest, CreateServerRepeatedPort) {
  ServerBuilder()
      .RegisterService(&g_service)
      .AddListeningPort(g_port, InsecureServerCredentials())
      .AddListeningPort(g_port, InsecureServerCredentials())
      .BuildAndStart()
      ->Shutdown();
}

TEST(ServerBuilderTest, CreateServerRepeatedPortWithDisallowedReusePort) {
  EXPECT_EQ(ServerBuilder()
                .RegisterService(&g_service)
                .AddListeningPort(g_port, InsecureServerCredentials())
                .AddListeningPort(g_port, InsecureServerCredentials())
                .AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0)
                .BuildAndStart(),
            nullptr);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
