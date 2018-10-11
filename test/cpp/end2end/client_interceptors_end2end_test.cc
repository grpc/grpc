/*
 *
 * Copyright 2018 gRPC authors.
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

#include <memory>
#include <vector>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/codegen/client_interceptor.h>
#include <grpcpp/impl/codegen/proto_utils.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {
namespace {

class ClientInterceptorsEnd2endTest : public ::testing::Test {
 protected:
  ClientInterceptorsEnd2endTest() {
    int port = grpc_pick_unused_port_or_die();

    ServerBuilder builder;
    server_address_ = "localhost:" + std::to_string(port);
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  ~ClientInterceptorsEnd2endTest() { server_->Shutdown(); }

  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
};

class LoggingInterceptor : public experimental::ClientInterceptor {
 public:
  LoggingInterceptor(experimental::ClientRpcInfo* info) { info_ = info; }

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    gpr_log(GPR_ERROR, "here\n");
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      gpr_log(GPR_ERROR, "here\n");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      gpr_log(GPR_ERROR, "here\n");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_CLOSE)) {
      gpr_log(GPR_ERROR, "here\n");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      gpr_log(GPR_ERROR, "here\n");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_MESSAGE)) {
      gpr_log(GPR_ERROR, "here\n");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_STATUS)) {
      gpr_log(GPR_ERROR, "here\n");
    }
    gpr_log(GPR_ERROR, "here\n");
    methods->Proceed();
  }

 private:
  experimental::ClientRpcInfo* info_;
};

class LoggingInterceptorFactory
    : public experimental::ClientInterceptorFactoryInterface {
 public:
  virtual experimental::ClientInterceptor* CreateClientInterceptor(
      experimental::ClientRpcInfo* info) override {
    return new LoggingInterceptor(info);
  }
};

TEST_F(ClientInterceptorsEnd2endTest, ClientInterceptorLoggingTest) {
  ChannelArguments args;
  auto creators = std::unique_ptr<std::vector<
      std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>>(
      new std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  creators->push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.set_message("Hello");
  EchoResponse resp;
  Status s = stub->Echo(&ctx, req, &resp);
  EXPECT_EQ(s.ok(), true);
  std::cout << resp.message() << "\n";
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
