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
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/client_interceptor.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/interceptors_util.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"
#include "test/cpp/util/string_ref_helper.h"

#include <gtest/gtest.h>

namespace grpc {
namespace testing {
namespace {

/* Hijacks Echo RPC and fills in the expected values */
class HijackingInterceptor : public experimental::Interceptor {
 public:
  HijackingInterceptor(experimental::ClientRpcInfo* info) {
    info_ = info;
    // Make sure it is the right method
    EXPECT_EQ(strcmp("/grpc.testing.EchoTestService/Echo", info->method()), 0);
    EXPECT_EQ(info->type(), experimental::ClientRpcInfo::Type::UNARY);
  }

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    bool hijack = false;
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* map = methods->GetSendInitialMetadata();
      // Check that we can see the test metadata
      ASSERT_EQ(map->size(), static_cast<unsigned>(1));
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
      EXPECT_TRUE(
          SerializationTraits<EchoRequest>::Deserialize(&copied_buffer, &req)
              .ok());
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
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
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
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
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
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
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
  virtual experimental::Interceptor* CreateClientInterceptor(
      experimental::ClientRpcInfo* info) override {
    return new HijackingInterceptor(info);
  }
};

class HijackingInterceptorMakesAnotherCall : public experimental::Interceptor {
 public:
  HijackingInterceptorMakesAnotherCall(experimental::ClientRpcInfo* info) {
    info_ = info;
    // Make sure it is the right method
    EXPECT_EQ(strcmp("/grpc.testing.EchoTestService/Echo", info->method()), 0);
  }

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* map = methods->GetSendInitialMetadata();
      // Check that we can see the test metadata
      ASSERT_EQ(map->size(), static_cast<unsigned>(1));
      auto iterator = map->begin();
      EXPECT_EQ("testkey", iterator->first);
      EXPECT_EQ("testvalue", iterator->second);
      // Make a copy of the map
      metadata_map_ = *map;
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      EchoRequest req;
      auto* buffer = methods->GetSendMessage();
      auto copied_buffer = *buffer;
      EXPECT_TRUE(
          SerializationTraits<EchoRequest>::Deserialize(&copied_buffer, &req)
              .ok());
      EXPECT_EQ(req.message(), "Hello");
      req_ = req;
      stub_ = grpc::testing::EchoTestService::NewStub(
          methods->GetInterceptedChannel());
      ctx_.AddMetadata(metadata_map_.begin()->first,
                       metadata_map_.begin()->second);
      stub_->experimental_async()->Echo(&ctx_, &req_, &resp_,
                                        [this, methods](Status s) {
                                          EXPECT_EQ(s.ok(), true);
                                          EXPECT_EQ(resp_.message(), "Hello");
                                          methods->Hijack();
                                        });
      // There isn't going to be any other interesting operation in this batch,
      // so it is fine to return
      return;
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_CLOSE)) {
      // Got nothing to do here for now
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      auto* map = methods->GetRecvInitialMetadata();
      // Got nothing better to do here for now
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_MESSAGE)) {
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      // Check that we got the hijacked message, and re-insert the expected
      // message
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
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_RECV_INITIAL_METADATA)) {
      auto* map = methods->GetRecvInitialMetadata();
      // Got nothing better to do here at the moment
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_RECV_MESSAGE)) {
      // Insert a different message than expected
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      resp->set_message(resp_.message());
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_RECV_STATUS)) {
      auto* map = methods->GetRecvTrailingMetadata();
      // insert the metadata that we want
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
      map->insert(std::make_pair("testkey", "testvalue"));
      auto* status = methods->GetRecvStatus();
      *status = Status(StatusCode::OK, "");
    }

    methods->Proceed();
  }

 private:
  experimental::ClientRpcInfo* info_;
  std::multimap<grpc::string, grpc::string> metadata_map_;
  ClientContext ctx_;
  EchoRequest req_;
  EchoResponse resp_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
};

class HijackingInterceptorMakesAnotherCallFactory
    : public experimental::ClientInterceptorFactoryInterface {
 public:
  virtual experimental::Interceptor* CreateClientInterceptor(
      experimental::ClientRpcInfo* info) override {
    return new HijackingInterceptorMakesAnotherCall(info);
  }
};

class LoggingInterceptor : public experimental::Interceptor {
 public:
  LoggingInterceptor(experimental::ClientRpcInfo* info) { info_ = info; }

  virtual void Intercept(experimental::InterceptorBatchMethods* methods) {
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* map = methods->GetSendInitialMetadata();
      // Check that we can see the test metadata
      ASSERT_EQ(map->size(), static_cast<unsigned>(1));
      auto iterator = map->begin();
      EXPECT_EQ("testkey", iterator->first);
      EXPECT_EQ("testvalue", iterator->second);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      EchoRequest req;
      auto* buffer = methods->GetSendMessage();
      auto copied_buffer = *buffer;
      EXPECT_TRUE(
          SerializationTraits<EchoRequest>::Deserialize(&copied_buffer, &req)
              .ok());
      EXPECT_TRUE(req.message().find("Hello") == 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_CLOSE)) {
      // Got nothing to do here for now
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      auto* map = methods->GetRecvInitialMetadata();
      // Got nothing better to do here for now
      EXPECT_EQ(map->size(), static_cast<unsigned>(0));
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_MESSAGE)) {
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      EXPECT_TRUE(resp->message().find("Hello") == 0);
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
  virtual experimental::Interceptor* CreateClientInterceptor(
      experimental::ClientRpcInfo* info) override {
    return new LoggingInterceptor(info);
  }
};

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

TEST_F(ClientInterceptorsEnd2endTest, ClientInterceptorLoggingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  creators.push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  // Add 20 dummy interceptors
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  MakeCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ClientInterceptorsEnd2endTest, ClientInterceptorHijackingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  // Add 20 dummy interceptors before hijacking interceptor
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  creators.push_back(std::unique_ptr<HijackingInterceptorFactory>(
      new HijackingInterceptorFactory()));
  // Add 20 dummy interceptors after hijacking interceptor
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));

  MakeCall(channel);
  // Make sure only 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ClientInterceptorsEnd2endTest, ClientInterceptorLogThenHijackTest) {
  ChannelArguments args;
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  creators.push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  creators.push_back(std::unique_ptr<HijackingInterceptorFactory>(
      new HijackingInterceptorFactory()));
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));

  MakeCall(channel);
}

TEST_F(ClientInterceptorsEnd2endTest,
       ClientInterceptorHijackingMakesAnotherCallTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  // Add 5 dummy interceptors before hijacking interceptor
  creators.reserve(5);
  for (auto i = 0; i < 5; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  creators.push_back(
      std::unique_ptr<experimental::ClientInterceptorFactoryInterface>(
          new HijackingInterceptorMakesAnotherCallFactory()));
  // Add 7 dummy interceptors after hijacking interceptor
  for (auto i = 0; i < 7; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = server_->experimental().InProcessChannelWithInterceptors(
      args, std::move(creators));

  MakeCall(channel);
  // Make sure all interceptors were run once, since the hijacking interceptor
  // makes an RPC on the intercepted channel
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 12);
}

TEST_F(ClientInterceptorsEnd2endTest,
       ClientInterceptorLoggingTestWithCallback) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  creators.push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  // Add 20 dummy interceptors
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = server_->experimental().InProcessChannelWithInterceptors(
      args, std::move(creators));
  MakeCallbackCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ClientInterceptorsEnd2endTest,
       ClientInterceptorFactoryAllowsNullptrReturn) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  creators.push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  // Add 20 dummy interceptors and 20 null interceptors
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
    creators.push_back(
        std::unique_ptr<NullInterceptorFactory>(new NullInterceptorFactory()));
  }
  auto channel = server_->experimental().InProcessChannelWithInterceptors(
      args, std::move(creators));
  MakeCallbackCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

class ClientInterceptorsStreamingEnd2endTest : public ::testing::Test {
 protected:
  ClientInterceptorsStreamingEnd2endTest() {
    int port = grpc_pick_unused_port_or_die();

    ServerBuilder builder;
    server_address_ = "localhost:" + std::to_string(port);
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  ~ClientInterceptorsStreamingEnd2endTest() { server_->Shutdown(); }

  std::string server_address_;
  EchoTestServiceStreamingImpl service_;
  std::unique_ptr<Server> server_;
};

TEST_F(ClientInterceptorsStreamingEnd2endTest, ClientStreamingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  creators.push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  // Add 20 dummy interceptors
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  MakeClientStreamingCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ClientInterceptorsStreamingEnd2endTest, ServerStreamingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  creators.push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  // Add 20 dummy interceptors
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  MakeServerStreamingCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ClientInterceptorsStreamingEnd2endTest, BidiStreamingTest) {
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  creators.push_back(std::unique_ptr<LoggingInterceptorFactory>(
      new LoggingInterceptorFactory()));
  // Add 20 dummy interceptors
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  MakeBidiStreamingCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
}

class ClientGlobalInterceptorEnd2endTest : public ::testing::Test {
 protected:
  ClientGlobalInterceptorEnd2endTest() {
    int port = grpc_pick_unused_port_or_die();

    ServerBuilder builder;
    server_address_ = "localhost:" + std::to_string(port);
    builder.AddListeningPort(server_address_, InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  ~ClientGlobalInterceptorEnd2endTest() { server_->Shutdown(); }

  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
};

TEST_F(ClientGlobalInterceptorEnd2endTest, DummyGlobalInterceptor) {
  // We should ideally be registering a global interceptor only once per
  // process, but for the purposes of testing, it should be fine to modify the
  // registered global interceptor when there are no ongoing gRPC operations
  DummyInterceptorFactory global_factory;
  experimental::RegisterGlobalClientInterceptorFactory(&global_factory);
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  // Add 20 dummy interceptors
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  MakeCall(channel);
  // Make sure all 20 dummy interceptors were run with the global interceptor
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 21);
  // Reset the global interceptor. This is again 'safe' because there are no
  // other ongoing gRPC operations
  experimental::RegisterGlobalClientInterceptorFactory(nullptr);
}

TEST_F(ClientGlobalInterceptorEnd2endTest, LoggingGlobalInterceptor) {
  // We should ideally be registering a global interceptor only once per
  // process, but for the purposes of testing, it should be fine to modify the
  // registered global interceptor when there are no ongoing gRPC operations
  LoggingInterceptorFactory global_factory;
  experimental::RegisterGlobalClientInterceptorFactory(&global_factory);
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  // Add 20 dummy interceptors
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  MakeCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
  // Reset the global interceptor. This is again 'safe' because there are no
  // other ongoing gRPC operations
  experimental::RegisterGlobalClientInterceptorFactory(nullptr);
}

TEST_F(ClientGlobalInterceptorEnd2endTest, HijackingGlobalInterceptor) {
  // We should ideally be registering a global interceptor only once per
  // process, but for the purposes of testing, it should be fine to modify the
  // registered global interceptor when there are no ongoing gRPC operations
  HijackingInterceptorFactory global_factory;
  experimental::RegisterGlobalClientInterceptorFactory(&global_factory);
  ChannelArguments args;
  DummyInterceptor::Reset();
  std::vector<std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
      creators;
  // Add 20 dummy interceptors
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<DummyInterceptorFactory>(
        new DummyInterceptorFactory()));
  }
  auto channel = experimental::CreateCustomChannelWithInterceptors(
      server_address_, InsecureChannelCredentials(), args, std::move(creators));
  MakeCall(channel);
  // Make sure all 20 dummy interceptors were run
  EXPECT_EQ(DummyInterceptor::GetNumTimesRun(), 20);
  // Reset the global interceptor. This is again 'safe' because there are no
  // other ongoing gRPC operations
  experimental::RegisterGlobalClientInterceptorFactory(nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
