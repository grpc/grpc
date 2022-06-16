//
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
//

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/gprpp/host_port.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/interop/istio_echo_server_lib.h"

namespace grpc {
namespace testing {
namespace {

using proto::EchoRequest;
using proto::EchoResponse;
using proto::EchoTestService;
using proto::ForwardEchoRequest;
using proto::ForwardEchoResponse;

class EchoTest : public ::testing::Test {
 protected:
  EchoTest() : echo_test_service_impl_("hostname") {
    ServerBuilder builder;
    builder.RegisterService(&echo_test_service_impl_);
    int port = grpc_pick_unused_port_or_die();
    server_address_ = grpc_core::JoinHostPort("localhost", port);
    builder.AddListeningPort(grpc_core::JoinHostPort("localhost", port),
                             InsecureServerCredentials());
    server_ = builder.BuildAndStart();
    auto channel = CreateChannel(server_address_, InsecureChannelCredentials());
    stub_ = EchoTestService::NewStub(channel);
  }

  EchoTestServiceImpl echo_test_service_impl_;
  std::string server_address_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<EchoTestService::Stub> stub_;
};

TEST_F(EchoTest, SimpleEchoTest) {
  ClientContext context;
  EchoRequest request;
  EchoResponse response;
  request.set_message("hello");
  auto status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_THAT(response.message(),
              ::testing::AllOf(::testing::HasSubstr("StatusCode=200\n"),
                               ::testing::HasSubstr("Hostname=hostname\n"),
                               ::testing::HasSubstr("Echo=hello\n"),
                               ::testing::HasSubstr("Host="),
                               ::testing::HasSubstr("IP=")));
}

TEST_F(EchoTest, ForwardEchoTest) {
  ClientContext context;
  ForwardEchoRequest request;
  ForwardEchoResponse response;
  request.set_count(3);
  request.set_qps(1);
  request.set_timeout_micros(20 * 1000 * 1000);  // 20 seconds
  request.set_url(absl::StrCat("grpc://", server_address_));
  request.set_message("hello");
  auto status = stub_->ForwardEcho(&context, request, &response);
  EXPECT_TRUE(status.ok());
  for (int i = 0; i < 3; ++i) {
    EXPECT_THAT(
        response.output()[i],
        ::testing::AllOf(
            ::testing::HasSubstr(
                absl::StrFormat("[%d body] StatusCode=200\n", i)),
            ::testing::HasSubstr(
                absl::StrFormat("[%d body] Hostname=hostname\n", i)),
            ::testing::HasSubstr(absl::StrFormat("[%d body] Echo=hello\n", i)),
            ::testing::HasSubstr(absl::StrFormat("[%d body] Host=", i)),
            ::testing::HasSubstr(absl::StrFormat("[%d body] IP=", i))));
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
