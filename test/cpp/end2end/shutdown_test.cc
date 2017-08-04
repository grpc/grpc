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

#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/support/env.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

namespace grpc {
namespace testing {

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  explicit TestServiceImpl(gpr_event* ev) : ev_(ev) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    gpr_event_set(ev_, (void*)1);
    while (!context->IsCancelled()) {
    }
    return Status::OK;
  }

 private:
  gpr_event* ev_;
};

class ShutdownTest : public ::testing::TestWithParam<string> {
 public:
  ShutdownTest() : shutdown_(false), service_(&ev_) { gpr_event_init(&ev_); }

  void SetUp() override {
    port_ = grpc_pick_unused_port_or_die();
    server_ = SetUpServer(port_);
  }

  std::unique_ptr<Server> SetUpServer(const int port) {
    grpc::string server_address = "localhost:" + to_string(port);

    ServerBuilder builder;
    auto server_creds =
        GetCredentialsProvider()->GetServerCredentials(GetParam());
    builder.AddListeningPort(server_address, server_creds);
    builder.RegisterService(&service_);
    std::unique_ptr<Server> server = builder.BuildAndStart();
    return server;
  }

  void TearDown() override { GPR_ASSERT(shutdown_); }

  void ResetStub() {
    string target = "dns:localhost:" + to_string(port_);
    ChannelArguments args;
    auto channel_creds =
        GetCredentialsProvider()->GetChannelCredentials(GetParam(), &args);
    channel_ = CreateCustomChannel(target, channel_creds, args);
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
  }

  string to_string(const int number) {
    std::stringstream strs;
    strs << number;
    return strs.str();
  }

  void SendRequest() {
    EchoRequest request;
    EchoResponse response;
    request.set_message("Hello");
    ClientContext context;
    GPR_ASSERT(!shutdown_);
    Status s = stub_->Echo(&context, request, &response);
    GPR_ASSERT(shutdown_);
  }

 protected:
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  bool shutdown_;
  int port_;
  gpr_event ev_;
  TestServiceImpl service_;
};

std::vector<string> GetAllCredentialsTypeList() {
  std::vector<grpc::string> credentials_types;
  if (GetCredentialsProvider()->GetChannelCredentials(kInsecureCredentialsType,
                                                      nullptr) != nullptr) {
    credentials_types.push_back(kInsecureCredentialsType);
  }
  auto sec_list = GetCredentialsProvider()->GetSecureCredentialsTypeList();
  for (auto sec = sec_list.begin(); sec != sec_list.end(); sec++) {
    credentials_types.push_back(*sec);
  }
  GPR_ASSERT(!credentials_types.empty());

  std::string credentials_type_list("credentials types:");
  for (const string& type : credentials_types) {
    credentials_type_list.append(" " + type);
  }
  gpr_log(GPR_INFO, "%s", credentials_type_list.c_str());
  return credentials_types;
}

INSTANTIATE_TEST_CASE_P(End2EndShutdown, ShutdownTest,
                        ::testing::ValuesIn(GetAllCredentialsTypeList()));

// TODO(ctiller): leaked objects in this test
TEST_P(ShutdownTest, ShutdownTest) {
  ResetStub();

  // send the request in a background thread
  std::thread thr(std::bind(&ShutdownTest::SendRequest, this));

  // wait for the server to get the event
  gpr_event_wait(&ev_, gpr_inf_future(GPR_CLOCK_MONOTONIC));

  shutdown_ = true;

  // shutdown should trigger cancellation causing everything to shutdown
  auto deadline =
      std::chrono::system_clock::now() + std::chrono::microseconds(100);
  server_->Shutdown(deadline);
  EXPECT_GE(std::chrono::system_clock::now(), deadline);

  thr.join();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
