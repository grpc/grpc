//
//
// Copyright 2020 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/server_builder.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {
namespace {

class XdsCredentialsEnd2EndFallbackTest
    : public ::testing::TestWithParam<const char*> {
 protected:
  XdsCredentialsEnd2EndFallbackTest() {
    int port = grpc_pick_unused_port_or_die();
    ServerBuilder builder;
    server_address_ = "localhost:" + std::to_string(port);
    builder.AddListeningPort(
        server_address_,
        GetCredentialsProvider()->GetServerCredentials(GetParam()));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
};

TEST_P(XdsCredentialsEnd2EndFallbackTest, NoXdsSchemeInTarget) {
  // Target does not use 'xds:///' scheme and should result in using fallback
  // credentials.
  ChannelArguments args;
  auto channel = grpc::CreateCustomChannel(
      server_address_,
      grpc::XdsCredentials(
          GetCredentialsProvider()->GetChannelCredentials(GetParam(), &args)),
      args);
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.set_message("Hello");
  EchoResponse resp;
  Status s = stub->Echo(&ctx, req, &resp);
  EXPECT_EQ(s.ok(), true);
  EXPECT_EQ(resp.message(), "Hello");
}

class XdsServerCredentialsEnd2EndFallbackTest
    : public ::testing::TestWithParam<const char*> {
 protected:
  XdsServerCredentialsEnd2EndFallbackTest() {
    int port = grpc_pick_unused_port_or_die();
    // Build a server that is not xDS enabled but uses XdsServerCredentials.
    ServerBuilder builder;
    server_address_ = "localhost:" + std::to_string(port);
    builder.AddListeningPort(
        server_address_,
        grpc::XdsServerCredentials(
            GetCredentialsProvider()->GetServerCredentials(GetParam())));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
};

TEST_P(XdsServerCredentialsEnd2EndFallbackTest, Basic) {
  ChannelArguments args;
  auto channel = grpc::CreateCustomChannel(
      server_address_,
      GetCredentialsProvider()->GetChannelCredentials(GetParam(), &args), args);
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.set_message("Hello");
  EchoResponse resp;
  Status s = stub->Echo(&ctx, req, &resp);
  EXPECT_EQ(s.ok(), true);
  EXPECT_EQ(resp.message(), "Hello");
}

INSTANTIATE_TEST_SUITE_P(XdsCredentialsEnd2EndFallback,
                         XdsCredentialsEnd2EndFallbackTest,
                         ::testing::ValuesIn(std::vector<const char*>(
                             {kInsecureCredentialsType, kTlsCredentialsType})));

INSTANTIATE_TEST_SUITE_P(XdsServerCredentialsEnd2EndFallback,
                         XdsServerCredentialsEnd2EndFallbackTest,
                         ::testing::ValuesIn(std::vector<const char*>(
                             {kInsecureCredentialsType, kTlsCredentialsType})));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
