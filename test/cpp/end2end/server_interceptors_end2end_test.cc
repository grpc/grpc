//
//
// Copyright 2018 gRPC authors.
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

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/proto_utils.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/server_interceptor.h>

#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/interceptors_util.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

namespace grpc {
namespace testing {
namespace {

class LoggingInterceptor : public experimental::Interceptor {
 public:
  explicit LoggingInterceptor(experimental::ServerRpcInfo* info) {
    // Check the method name and compare to the type
    const char* method = info->method();
    experimental::ServerRpcInfo::Type type = info->type();

    // Check that we use one of our standard methods with expected type.
    // Also allow the health checking service.
    // We accept BIDI_STREAMING for Echo in case it's an AsyncGenericService
    // being tested (the GenericRpc test).
    // The empty method is for the Unimplemented requests that arise
    // when draining the CQ.
    EXPECT_TRUE(
        strstr(method, "/grpc.health") == method ||
        (strcmp(method, "/grpc.testing.EchoTestService/Echo") == 0 &&
         (type == experimental::ServerRpcInfo::Type::UNARY ||
          type == experimental::ServerRpcInfo::Type::BIDI_STREAMING)) ||
        (strcmp(method, "/grpc.testing.EchoTestService/RequestStream") == 0 &&
         type == experimental::ServerRpcInfo::Type::CLIENT_STREAMING) ||
        (strcmp(method, "/grpc.testing.EchoTestService/ResponseStream") == 0 &&
         type == experimental::ServerRpcInfo::Type::SERVER_STREAMING) ||
        (strcmp(method, "/grpc.testing.EchoTestService/BidiStream") == 0 &&
         type == experimental::ServerRpcInfo::Type::BIDI_STREAMING) ||
        strcmp(method, "/grpc.testing.EchoTestService/Unimplemented") == 0 ||
        (strcmp(method, "") == 0 &&
         type == experimental::ServerRpcInfo::Type::BIDI_STREAMING));
  }

  void Intercept(experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* map = methods->GetSendInitialMetadata();
      // Got nothing better to do here for now
      EXPECT_EQ(map->size(), 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      EchoRequest req;
      auto* buffer = methods->GetSerializedSendMessage();
      auto copied_buffer = *buffer;
      EXPECT_TRUE(
          SerializationTraits<EchoRequest>::Deserialize(&copied_buffer, &req)
              .ok());
      EXPECT_TRUE(req.message().find("Hello") == 0);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
      auto* map = methods->GetSendTrailingMetadata();
      bool found = false;
      // Check that we received the metadata as an echo
      for (const auto& [key, value] : *map) {
        found = absl::StartsWith(key, "testkey") &&
                absl::StartsWith(value, "testvalue");
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
      for (const auto& [key, value] : *map) {
        found = key.starts_with("testkey") && value.starts_with("testvalue");
        if (found) break;
      }
      EXPECT_EQ(found, true);
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_MESSAGE)) {
      EchoResponse* resp =
          static_cast<EchoResponse*>(methods->GetRecvMessage());
      if (resp != nullptr) {
        EXPECT_TRUE(resp->message().find("Hello") == 0);
      }
    }
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::POST_RECV_CLOSE)) {
      // Got nothing interesting to do here
    }
    methods->Proceed();
  }
};

class LoggingInterceptorFactory
    : public experimental::ServerInterceptorFactoryInterface {
 public:
  experimental::Interceptor* CreateServerInterceptor(
      experimental::ServerRpcInfo* info) override {
    return new LoggingInterceptor(info);
  }
};

// Test if SendMessage function family works as expected for sync/callback apis
class SyncSendMessageTester : public experimental::Interceptor {
 public:
  explicit SyncSendMessageTester(experimental::ServerRpcInfo* /*info*/) {}

  void Intercept(experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      string old_msg =
          static_cast<const EchoRequest*>(methods->GetSendMessage())->message();
      EXPECT_EQ(old_msg.find("Hello"), 0u);
      new_msg_.set_message("World" + old_msg);
      methods->ModifySendMessage(&new_msg_);
    }
    methods->Proceed();
  }

 private:
  EchoRequest new_msg_;
};

class SyncSendMessageTesterFactory
    : public experimental::ServerInterceptorFactoryInterface {
 public:
  experimental::Interceptor* CreateServerInterceptor(
      experimental::ServerRpcInfo* info) override {
    return new SyncSendMessageTester(info);
  }
};

// Test if SendMessage function family works as expected for sync/callback apis
class SyncSendMessageVerifier : public experimental::Interceptor {
 public:
  explicit SyncSendMessageVerifier(experimental::ServerRpcInfo* /*info*/) {}

  void Intercept(experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)) {
      // Make sure that the changes made in SyncSendMessageTester persisted
      string old_msg =
          static_cast<const EchoRequest*>(methods->GetSendMessage())->message();
      EXPECT_EQ(old_msg.find("World"), 0u);

      // Remove the "World" part of the string that we added earlier
      new_msg_.set_message(old_msg.erase(0, 5));
      methods->ModifySendMessage(&new_msg_);

      // LoggingInterceptor verifies that changes got reverted
    }
    methods->Proceed();
  }

 private:
  EchoRequest new_msg_;
};

class SyncSendMessageVerifierFactory
    : public experimental::ServerInterceptorFactoryInterface {
 public:
  experimental::Interceptor* CreateServerInterceptor(
      experimental::ServerRpcInfo* info) override {
    return new SyncSendMessageVerifier(info);
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
            new SyncSendMessageTesterFactory()));
    creators.push_back(
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
            new SyncSendMessageVerifierFactory()));
    creators.push_back(
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
            new LoggingInterceptorFactory()));
    // Add 20 phony interceptor factories and null interceptor factories
    for (auto i = 0; i < 20; i++) {
      creators.push_back(std::make_unique<PhonyInterceptorFactory>());
      creators.push_back(std::make_unique<NullInterceptorFactory>());
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
  PhonyInterceptor::Reset();
  auto channel =
      grpc::CreateChannel(server_address_, InsecureChannelCredentials());
  MakeCall(channel);
  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);
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
            new SyncSendMessageTesterFactory()));
    creators.push_back(
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
            new SyncSendMessageVerifierFactory()));
    creators.push_back(
        std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
            new LoggingInterceptorFactory()));
    for (auto i = 0; i < 20; i++) {
      creators.push_back(std::make_unique<PhonyInterceptorFactory>());
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
  PhonyInterceptor::Reset();
  auto channel =
      grpc::CreateChannel(server_address_, InsecureChannelCredentials());
  MakeClientStreamingCall(channel);
  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ServerInterceptorsEnd2endSyncStreamingTest, ServerStreamingTest) {
  ChannelArguments args;
  PhonyInterceptor::Reset();
  auto channel =
      grpc::CreateChannel(server_address_, InsecureChannelCredentials());
  MakeServerStreamingCall(channel);
  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);
}

TEST_F(ServerInterceptorsEnd2endSyncStreamingTest, BidiStreamingTest) {
  ChannelArguments args;
  PhonyInterceptor::Reset();
  auto channel =
      grpc::CreateChannel(server_address_, InsecureChannelCredentials());
  MakeBidiStreamingCall(channel);
  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);
}

class ServerInterceptorsAsyncEnd2endTest : public ::testing::Test {};

TEST_F(ServerInterceptorsAsyncEnd2endTest, UnaryTest) {
  PhonyInterceptor::Reset();
  int port = grpc_pick_unused_port_or_die();
  string server_address = "localhost:" + std::to_string(port);
  ServerBuilder builder;
  EchoTestService::AsyncService service;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::vector<std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
      creators;
  creators.push_back(
      std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
          new LoggingInterceptorFactory()));
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::make_unique<PhonyInterceptorFactory>());
  }
  builder.experimental().SetInterceptorCreators(std::move(creators));
  auto cq = builder.AddCompletionQueue();
  auto server = builder.BuildAndStart();

  ChannelArguments args;
  auto channel =
      grpc::CreateChannel(server_address, InsecureChannelCredentials());
  auto stub = grpc::testing::EchoTestService::NewStub(channel);

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message("Hello");
  cli_ctx.AddMetadata("testkey", "testvalue");
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub->AsyncEcho(&cli_ctx, send_request, cq.get()));

  service.RequestEcho(&srv_ctx, &recv_request, &response_writer, cq.get(),
                      cq.get(), tag(2));

  response_reader->Finish(&recv_response, &recv_status, tag(4));

  Verifier().Expect(2, true).Verify(cq.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  EXPECT_TRUE(CheckMetadata(srv_ctx.client_metadata(), "testkey", "testvalue"));
  srv_ctx.AddTrailingMetadata("testkey", "testvalue");

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
  EXPECT_TRUE(CheckMetadata(cli_ctx.GetServerTrailingMetadata(), "testkey",
                            "testvalue"));

  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);

  server->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  cq->Shutdown();
  void* ignored_tag;
  bool ignored_ok;
  while (cq->Next(&ignored_tag, &ignored_ok)) {
  }
  grpc_recycle_unused_port(port);
}

TEST_F(ServerInterceptorsAsyncEnd2endTest, BidiStreamingTest) {
  PhonyInterceptor::Reset();
  int port = grpc_pick_unused_port_or_die();
  string server_address = "localhost:" + std::to_string(port);
  ServerBuilder builder;
  EchoTestService::AsyncService service;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::vector<std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
      creators;
  creators.push_back(
      std::unique_ptr<experimental::ServerInterceptorFactoryInterface>(
          new LoggingInterceptorFactory()));
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::make_unique<PhonyInterceptorFactory>());
  }
  builder.experimental().SetInterceptorCreators(std::move(creators));
  auto cq = builder.AddCompletionQueue();
  auto server = builder.BuildAndStart();

  ChannelArguments args;
  auto channel =
      grpc::CreateChannel(server_address, InsecureChannelCredentials());
  auto stub = grpc::testing::EchoTestService::NewStub(channel);

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncReaderWriter<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message("Hello");
  cli_ctx.AddMetadata("testkey", "testvalue");
  std::unique_ptr<ClientAsyncReaderWriter<EchoRequest, EchoResponse>>
      cli_stream(stub->AsyncBidiStream(&cli_ctx, cq.get(), tag(1)));

  service.RequestBidiStream(&srv_ctx, &srv_stream, cq.get(), cq.get(), tag(2));

  Verifier().Expect(1, true).Expect(2, true).Verify(cq.get());

  EXPECT_TRUE(CheckMetadata(srv_ctx.client_metadata(), "testkey", "testvalue"));
  srv_ctx.AddTrailingMetadata("testkey", "testvalue");

  cli_stream->Write(send_request, tag(3));
  srv_stream.Read(&recv_request, tag(4));
  Verifier().Expect(3, true).Expect(4, true).Verify(cq.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(5));
  cli_stream->Read(&recv_response, tag(6));
  Verifier().Expect(5, true).Expect(6, true).Verify(cq.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->WritesDone(tag(7));
  srv_stream.Read(&recv_request, tag(8));
  Verifier().Expect(7, true).Expect(8, false).Verify(cq.get());

  srv_stream.Finish(Status::OK, tag(9));
  cli_stream->Finish(&recv_status, tag(10));
  Verifier().Expect(9, true).Expect(10, true).Verify(cq.get());

  EXPECT_TRUE(recv_status.ok());
  EXPECT_TRUE(CheckMetadata(cli_ctx.GetServerTrailingMetadata(), "testkey",
                            "testvalue"));

  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);

  server->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  cq->Shutdown();
  void* ignored_tag;
  bool ignored_ok;
  while (cq->Next(&ignored_tag, &ignored_ok)) {
  }
  grpc_recycle_unused_port(port);
}

TEST_F(ServerInterceptorsAsyncEnd2endTest, GenericRPCTest) {
  PhonyInterceptor::Reset();
  int port = grpc_pick_unused_port_or_die();
  string server_address = "localhost:" + std::to_string(port);
  ServerBuilder builder;
  AsyncGenericService service;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterAsyncGenericService(&service);
  std::vector<std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
      creators;
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::make_unique<PhonyInterceptorFactory>());
  }
  builder.experimental().SetInterceptorCreators(std::move(creators));
  auto srv_cq = builder.AddCompletionQueue();
  CompletionQueue cli_cq;
  auto server = builder.BuildAndStart();

  ChannelArguments args;
  auto channel =
      grpc::CreateChannel(server_address, InsecureChannelCredentials());
  GenericStub generic_stub(channel);

  const std::string kMethodName("/grpc.cpp.test.util.EchoTestService/Echo");
  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  GenericServerContext srv_ctx;
  GenericServerAsyncReaderWriter stream(&srv_ctx);

  // The string needs to be long enough to test heap-based slice.
  send_request.set_message("Hello");
  cli_ctx.AddMetadata("testkey", "testvalue");

  CompletionQueue* cq = srv_cq.get();
  std::thread request_call([cq]() { Verifier().Expect(4, true).Verify(cq); });
  std::unique_ptr<GenericClientAsyncReaderWriter> call =
      generic_stub.PrepareCall(&cli_ctx, kMethodName, &cli_cq);
  call->StartCall(tag(1));
  Verifier().Expect(1, true).Verify(&cli_cq);
  std::unique_ptr<ByteBuffer> send_buffer =
      SerializeToByteBuffer(&send_request);
  call->Write(*send_buffer, tag(2));
  // Send ByteBuffer can be destroyed after calling Write.
  send_buffer.reset();
  Verifier().Expect(2, true).Verify(&cli_cq);
  call->WritesDone(tag(3));
  Verifier().Expect(3, true).Verify(&cli_cq);

  service.RequestCall(&srv_ctx, &stream, srv_cq.get(), srv_cq.get(), tag(4));

  request_call.join();
  EXPECT_EQ(kMethodName, srv_ctx.method());
  EXPECT_TRUE(CheckMetadata(srv_ctx.client_metadata(), "testkey", "testvalue"));
  srv_ctx.AddTrailingMetadata("testkey", "testvalue");

  ByteBuffer recv_buffer;
  stream.Read(&recv_buffer, tag(5));
  Verifier().Expect(5, true).Verify(srv_cq.get());
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  send_buffer = SerializeToByteBuffer(&send_response);
  stream.Write(*send_buffer, tag(6));
  send_buffer.reset();
  Verifier().Expect(6, true).Verify(srv_cq.get());

  stream.Finish(Status::OK, tag(7));
  // Shutdown srv_cq before we try to get the tag back, to verify that the
  // interception API handles completion queue shutdowns that take place before
  // all the tags are returned
  srv_cq->Shutdown();
  Verifier().Expect(7, true).Verify(srv_cq.get());

  recv_buffer.Clear();
  call->Read(&recv_buffer, tag(8));
  Verifier().Expect(8, true).Verify(&cli_cq);
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_response));

  call->Finish(&recv_status, tag(9));
  cli_cq.Shutdown();
  Verifier().Expect(9, true).Verify(&cli_cq);

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
  EXPECT_TRUE(CheckMetadata(cli_ctx.GetServerTrailingMetadata(), "testkey",
                            "testvalue"));

  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);

  server->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  void* ignored_tag;
  bool ignored_ok;
  while (cli_cq.Next(&ignored_tag, &ignored_ok)) {
  }
  while (srv_cq->Next(&ignored_tag, &ignored_ok)) {
  }
  grpc_recycle_unused_port(port);
}

TEST_F(ServerInterceptorsAsyncEnd2endTest, UnimplementedRpcTest) {
  PhonyInterceptor::Reset();
  int port = grpc_pick_unused_port_or_die();
  string server_address = "localhost:" + std::to_string(port);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  std::vector<std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
      creators;
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::make_unique<PhonyInterceptorFactory>());
  }
  builder.experimental().SetInterceptorCreators(std::move(creators));
  auto cq = builder.AddCompletionQueue();
  auto server = builder.BuildAndStart();

  ChannelArguments args;
  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(server_address, InsecureChannelCredentials());
  std::unique_ptr<grpc::testing::UnimplementedEchoService::Stub> stub;
  stub = grpc::testing::UnimplementedEchoService::NewStub(channel);
  EchoRequest send_request;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  send_request.set_message("Hello");
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
      stub->AsyncUnimplemented(&cli_ctx, send_request, cq.get()));

  response_reader->Finish(&recv_response, &recv_status, tag(4));
  Verifier().Expect(4, true).Verify(cq.get());

  EXPECT_EQ(StatusCode::UNIMPLEMENTED, recv_status.error_code());
  EXPECT_EQ("", recv_status.error_message());

  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);

  server->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  cq->Shutdown();
  void* ignored_tag;
  bool ignored_ok;
  while (cq->Next(&ignored_tag, &ignored_ok)) {
  }
  grpc_recycle_unused_port(port);
}

class ServerInterceptorsSyncUnimplementedEnd2endTest : public ::testing::Test {
};

TEST_F(ServerInterceptorsSyncUnimplementedEnd2endTest, UnimplementedRpcTest) {
  PhonyInterceptor::Reset();
  int port = grpc_pick_unused_port_or_die();
  string server_address = "localhost:" + std::to_string(port);
  ServerBuilder builder;
  TestServiceImpl service;
  builder.RegisterService(&service);
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  std::vector<std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
      creators;
  creators.reserve(20);
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::make_unique<PhonyInterceptorFactory>());
  }
  builder.experimental().SetInterceptorCreators(std::move(creators));
  auto server = builder.BuildAndStart();

  ChannelArguments args;
  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(server_address, InsecureChannelCredentials());
  std::unique_ptr<grpc::testing::UnimplementedEchoService::Stub> stub;
  stub = grpc::testing::UnimplementedEchoService::NewStub(channel);
  EchoRequest send_request;
  EchoResponse recv_response;

  ClientContext cli_ctx;
  send_request.set_message("Hello");
  Status recv_status =
      stub->Unimplemented(&cli_ctx, send_request, &recv_response);

  EXPECT_EQ(StatusCode::UNIMPLEMENTED, recv_status.error_code());
  EXPECT_EQ("", recv_status.error_message());

  // Make sure all 20 phony interceptors were run
  EXPECT_EQ(PhonyInterceptor::GetNumTimesRun(), 20);

  server->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  grpc_recycle_unused_port(port);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
