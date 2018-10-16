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

class DummyInterceptor : public experimental::ClientInterceptor {
 public:
  DummyInterceptor(experimental::ClientRpcInfo* info) {}

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      num_times_run_++;
    }
    methods->Proceed();
  }

  static void Reset() { num_times_run_.store(0); }

  static int GetNumTimesRun() { return num_times_run_.load(); }

 private:
  static std::atomic<int> num_times_run_;
};

std::atomic<int> DummyInterceptor::num_times_run_;

class DummyInterceptorFactory
    : public experimental::ClientInterceptorFactoryInterface {
 public:
  virtual experimental::ClientInterceptor* CreateClientInterceptor(
      experimental::ClientRpcInfo* info) override {
    return new DummyInterceptor(info);
  }
};

class HijackingInterceptor : public experimental::ClientInterceptor {
 public:
  HijackingInterceptor(experimental::ClientRpcInfo* info) {
    info_ = info;
    // Make sure it is the right method
    EXPECT_EQ(strcmp("/grpc.testing.EchoTestService/Echo", info->method()), 0);
  }

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    gpr_log(GPR_ERROR, "ran this");
    bool hijack = false;
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* map = methods->GetSendInitialMetadata();
      // Check that we can see the test metadata
      ASSERT_EQ(map->size(), 1);
      auto iterator = map->begin();
      EXPECT_EQ("testkey", iterator->first);
      EXPECT_EQ("testvalue", iterator->second);
      hijack = true;
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      EchoRequest req;
      auto* buffer = methods->GetSendMessage();
      auto copied_buffer = *buffer;
      SerializationTraits<EchoRequest>::Deserialize(&copied_buffer, &req);
      EXPECT_EQ(req.message(), "Hello");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_CLOSE)) {
      // Got nothing to do here for now
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      auto* map = methods->GetRecvInitialMetadata();
      // Got nothing better to do here for now
      EXPECT_EQ(map->size(), 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_MESSAGE)) {
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      // Check that we got the hijacked message, and re-insert the expected
      // message
      EXPECT_EQ(resp->message(), "Hello1");
      resp->set_message("Hello");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_STATUS)) {
      auto* map = methods->GetRecvTrailingMetadata();
      bool found = false;
      // Check that we received the metadata as an echo
      for (const auto& pair : *map) {
        found = pair.first.starts_with("testkey") &&
                pair.second.starts_with("testvalue");
        if (found) break;
      }
      EXPECT_EQ(found, true);
      auto* status = methods->GetRecvStatus();
      EXPECT_EQ(status->ok(), true);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_RECV_INITIAL_METADATA)) {
      auto* map = methods->GetRecvInitialMetadata();
      // Got nothing better to do here at the moment
      EXPECT_EQ(map->size(), 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_RECV_MESSAGE)) {
      // Insert a different message than expected
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      resp->set_message("Hello1");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_RECV_STATUS)) {
      auto* map = methods->GetRecvTrailingMetadata();
      // insert the metadata that we want
      EXPECT_EQ(map->size(), 0);
      map->insert(std::make_pair("testkey", "testvalue"));
      auto* status = methods->GetRecvStatus();
      *status = Status(StatusCode::OK, "");
    }
    if (hijack) {
      methods->Hijack();
    } else {
      methods->Proceed();
    }
  }

 private:
  experimental::ClientRpcInfo* info_;
};

class HijackingInterceptorFactory
    : public experimental::ClientInterceptorFactoryInterface {
 public:
  virtual experimental::ClientInterceptor* CreateClientInterceptor(
      experimental::ClientRpcInfo* info) override {
    return new HijackingInterceptor(info);
  }
};

class LoggingInterceptor : public experimental::ClientInterceptor {
 public:
  LoggingInterceptor(experimental::ClientRpcInfo* info) {
    info_ = info;
    // Make sure it is the right method
    EXPECT_EQ(strcmp("/grpc.testing.EchoTestService/Echo", info->method()), 0);
  }

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    gpr_log(GPR_ERROR, "ran this");
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* map = methods->GetSendInitialMetadata();
      // Check that we can see the test metadata
      ASSERT_EQ(map->size(), 1);
      auto iterator = map->begin();
      EXPECT_EQ("testkey", iterator->first);
      EXPECT_EQ("testvalue", iterator->second);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      EchoRequest req;
      auto* buffer = methods->GetSendMessage();
      auto copied_buffer = *buffer;
      SerializationTraits<EchoRequest>::Deserialize(&copied_buffer, &req);
      EXPECT_EQ(req.message(), "Hello");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_CLOSE)) {
      // Got nothing to do here for now
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      auto* map = methods->GetRecvInitialMetadata();
      // Got nothing better to do here for now
      EXPECT_EQ(map->size(), 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_MESSAGE)) {
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      EXPECT_EQ(resp->message(), "Hello");
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_STATUS)) {
      auto* map = methods->GetRecvTrailingMetadata();
      bool found = false;
      // Check that we received the metadata as an echo
      for (const auto& pair : *map) {
        found = pair.first.starts_with("testkey") &&
                pair.second.starts_with("testvalue");
        if (found) break;
      }
      EXPECT_EQ(found, true);
      auto* status = methods->GetRecvStatus();
      EXPECT_EQ(status->ok(), true);
    }
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
  DummyInterceptor::Reset();
  auto creators = std::unique_ptr<std::vector<
      std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>>(
      new std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  creators->push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  // Add 20 dummy interceptors
  for (auto i = 0; i < 20; i++) {
    creators->push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.mutable_param()->set_echo_metadata(true);
  ctx.AddMetadata("testkey", "testvalue");
  req.set_message("Hello");
  EchoResponse resp;
  Status s = stub->Echo(&ctx, req, &resp);
  EXPECT_EQ(s.ok(), true);
  EXPECT_EQ(resp.message(), "Hello");
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ClientInterceptorsEnd2endTest, ClientInterceptorHijackingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  auto creators = std::unique_ptr<std::vector<
      std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>>(
      new std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  // Add 10 dummy interceptors before hijacking interceptor
  for (auto i = 0; i < 20; i++) {
    creators->push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  creators->push_back(std::unique_ptr<HijackingInterceptorFactory>(
      new HijackingInterceptorFactory()));
  // Add 10 dummy interceptors after hijacking interceptor
  for (auto i = 0; i < 20; i++) {
    creators->push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.mutable_param()->set_echo_metadata(true);
  ctx.AddMetadata("testkey", "testvalue");
  req.set_message("Hello");
  EchoResponse resp;
  Status s = stub->Echo(&ctx, req, &resp);
  EXPECT_EQ(s.ok(), true);
  EXPECT_EQ(resp.message(), "Hello");
  // Make sure only 10 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ClientInterceptorsEnd2endTest, ClientInterceptorLogThenHijackTest) {
  ChannelArguments args;
  auto creators = std::unique_ptr<std::vector<
      std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>>(
      new std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
  creators->push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  creators->push_back(std::unique_ptr<HijackingInterceptorFactory>(
      new HijackingInterceptorFactory()));
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  req.mutable_param()->set_echo_metadata(true);
  ctx.AddMetadata("testkey", "testvalue");
  req.set_message("Hello");
  EchoResponse resp;
  Status s = stub->Echo(&ctx, req, &resp);
  EXPECT_EQ(s.ok(), true);
  EXPECT_EQ(resp.message(), "Hello");
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
