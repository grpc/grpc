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
#include <grpcpp/impl/codegen/proto_utils.h>
#include <grpcpp/impl/codegen/server_interceptor.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/interceptors_util.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {
namespace {

/* This interceptor does nothing. Just keeps a global count on the number of
 * times it was invoked. */
class DummyInterceptor : public experimental::Interceptor {
 public:
  DummyInterceptor(experimental::ServerRpcInfo* info) {}

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      num_times_run_++;
    } else if (methods->QueryInterceptionHookPoint(
                   experimental::InterceptionHookPoints::
                       POST_RECV_INITIAL_METADATA)) {
      num_times_run_reverse_++;
    }
    methods->Proceed();
  }

  static void Reset() {
    num_times_run_.store(0);
    num_times_run_reverse_.store(0);
  }

  static int GetNumTimesRun() {
    EXPECT_EQ(num_times_run_.load(), num_times_run_reverse_.load());
    return num_times_run_.load();
  }

 private:
  static std::atomic<int> num_times_run_;
  static std::atomic<int> num_times_run_reverse_;
};

std::atomic<int> DummyInterceptor::num_times_run_;
std::atomic<int> DummyInterceptor::num_times_run_reverse_;

class DummyInterceptorFactory
    : public experimental::ServerInterceptorFactoryInterface {
 public:
  virtual experimental::Interceptor* CreateServerInterceptor(
      experimental::ServerRpcInfo* info) override {
    return new DummyInterceptor(info);
  }
};

class LoggingInterceptor : public experimental::Interceptor {
 public:
  LoggingInterceptor(experimental::ServerRpcInfo* info) { info_ = info; }

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    // gpr_log(GPR_ERROR, "ran this");
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* map = methods->GetSendInitialMetadata();
      // Got nothing better to do here for now
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      EchoRequest req;
      auto* buffer = methods->GetSendMessage();
      auto copied_buffer = *buffer;
      SerializationTraits<EchoRequest>::Deserialize(&copied_buffer, &req);
      EXPECT_TRUE(req.message().find("Hello") == 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
      auto* map = methods->GetSendTrailingMetadata();
      bool found = false;
      // Check that we received the metadata as an echo
      for (const auto& pair : *map) {
        found = pair.first.find("testkey") == 0 &&
                pair.second.find("testvalue") == 0;
        if (found) break;
      }
      EXPECT_EQ(found, true);
      auto status = methods->GetSendStatus();
      EXPECT_EQ(status.ok(), true);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      auto* map = methods->GetRecvInitialMetadata();
      bool found = false;
      // Check that we received the metadata as an echo
      for (const auto& pair : *map) {
        found = pair.first.find("testkey") == 0 &&
                pair.second.find("testvalue") == 0;
        if (found) break;
      }
      EXPECT_EQ(found, true);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_MESSAGE)) {
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      EXPECT_TRUE(resp->message().find("Hello") == 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_CLOSE)) {
      // Got nothing interesting to do here
    }
    methods->Proceed();
  }

 private:
  experimental::ServerRpcInfo* info_;
};

class LoggingInterceptorFactory
    : public experimental::ServerInterceptorFactoryInterface {
 public:
  virtual experimental::Interceptor* CreateServerInterceptor(
      experimental::ServerRpcInfo* info) override {
    return new LoggingInterceptor(info);
  }
};

void MakeBidiStreamingCall(const std::shared_ptr<Channel>& channel) {
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext ctx;
  EchoRequest req;
  EchoResponse resp;
  ctx.AddMetadata("testkey", "testvalue");
  auto stream = stub->BidiStream(&ctx);
  for (auto i = 0; i < 10; i++) {
    req.set_message("Hello" + std::to_string(i));
    stream->Write(req);
    stream->Read(&resp);
    EXPECT_EQ(req.message(), resp.message());
  }
  ASSERT_TRUE(stream->WritesDone());
  Status s = stream->Finish();
  EXPECT_EQ(s.ok(), true);
}

class ServerInterceptorsEnd2endSyncUnaryTest : public ::testing::Test {
 protected:
  ServerInterceptorsEnd2endSyncUnaryTest() {
    int port = grpc_pick_unused_port_or_die();

    ServerBuilder builder;
    server_address_ = "localhost:" + std::to_string(port);
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&service_);

    std::vector<
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
        creators;
    creators.push_back(
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
            new LoggingInterceptorFactory()));
    for (auto i = 0; i < 20; i++) {
      creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
          new DummyInterceptorFactory()));
    }
    builder.experimental().SetInterceptorCreators(std::move(creators));
    server_ = builder.BuildAndStart();
  }
  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
};

TEST_F(ServerInterceptorsEnd2endSyncUnaryTest, UnaryTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  auto channel = CreateChannel(server_address_, InsecureChannelCredentials());
  MakeCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

class ServerInterceptorsEnd2endSyncStreamingTest : public ::testing::Test {
 protected:
  ServerInterceptorsEnd2endSyncStreamingTest() {
    int port = grpc_pick_unused_port_or_die();

    ServerBuilder builder;
    server_address_ = "localhost:" + std::to_string(port);
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&service_);

    std::vector<
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
        creators;
    creators.push_back(
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
            new LoggingInterceptorFactory()));
    for (auto i = 0; i < 20; i++) {
      creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
          new DummyInterceptorFactory()));
    }
    builder.experimental().SetInterceptorCreators(std::move(creators));
    server_ = builder.BuildAndStart();
  }
  std::string server_address_;
  EchoTestServiceStreamingImpl service_;
  std::unique_ptr<Server> server_;
};

TEST_F(ServerInterceptorsEnd2endSyncStreamingTest, ClientStreamingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  auto channel = CreateChannel(server_address_, InsecureChannelCredentials());
  MakeClientStreamingCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ServerInterceptorsEnd2endSyncStreamingTest, ServerStreamingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  auto channel = CreateChannel(server_address_, InsecureChannelCredentials());
  MakeServerStreamingCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ServerInterceptorsEnd2endSyncStreamingTest, BidiStreamingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  auto channel = CreateChannel(server_address_, InsecureChannelCredentials());
  MakeBidiStreamingCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
