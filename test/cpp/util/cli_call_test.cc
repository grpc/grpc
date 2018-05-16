/*
 *
 * Copyright 2015 gRPC authors.
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

#include "test/cpp/util/cli_call.h"

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/string_ref_helper.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

namespace grpc {
namespace testing {

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    if (!context->client_metadata().empty()) {
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
               iter = context->client_metadata().begin();
           iter != context->client_metadata().end(); ++iter) {
        context->AddInitialMetadata(ToString(iter->first),
                                    ToString(iter->second));
      }
    }
    context->AddTrailingMetadata("trailing_key", "trailing_value");
    response->set_message(request->message());
    return Status::OK;
  }
};

class CliCallTest : public ::testing::Test {
 protected:
  CliCallTest() {}

  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() override { server_->Shutdown(); }

  void ResetStub() {
    channel_ =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
};

// Send a rpc with a normal stub and then a CliCall. Verify they match.
TEST_F(CliCallTest, SimpleRpc) {
  ResetStub();
  // Normal stub.
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  context.AddMetadata("key1", "val1");
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.ok());

  const grpc::string kMethod("/grpc.testing.EchoTestService/Echo");
  grpc::string request_bin, response_bin, expected_response_bin;
  EXPECT_TRUE(request.SerializeToString(&request_bin));
  EXPECT_TRUE(response.SerializeToString(&expected_response_bin));
  std::multimap<grpc::string, grpc::string> client_metadata;
  std::multimap<grpc::string_ref, grpc::string_ref> server_initial_metadata,
      server_trailing_metadata;
  client_metadata.insert(std::pair<grpc::string, grpc::string>("key1", "val1"));
  Status s2 = CliCall::Call(channel_, kMethod, request_bin, &response_bin,
                            client_metadata, &server_initial_metadata,
                            &server_trailing_metadata);
  EXPECT_TRUE(s2.ok());

  EXPECT_EQ(expected_response_bin, response_bin);
  EXPECT_EQ(context.GetServerInitialMetadata(), server_initial_metadata);
  EXPECT_EQ(context.GetServerTrailingMetadata(), server_trailing_metadata);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
