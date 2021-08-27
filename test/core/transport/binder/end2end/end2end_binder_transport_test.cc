// Copyright 2021 gRPC authors.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>
#include <string>
#include <thread>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/time/time.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader_impl.h"
#include "test/core/transport/binder/end2end/fake_binder.h"
#include "test/core/transport/binder/end2end/testing_channel_create.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc_binder {

namespace {

class End2EndBinderTransportTest
    : public ::testing::TestWithParam<absl::Duration> {
 public:
  End2EndBinderTransportTest() {
    end2end_testing::g_transaction_processor =
        new end2end_testing::TransactionProcessor(GetParam());
  }

  ~End2EndBinderTransportTest() override {
    delete end2end_testing::g_transaction_processor;
  }

  static void SetUpTestSuite() { grpc_init(); }
  static void TearDownTestSuite() { grpc_shutdown(); }

  std::shared_ptr<grpc::Channel> BinderChannel(
      grpc::Server* server, const grpc::ChannelArguments& args) {
    return end2end_testing::BinderChannelForTesting(server, args);
  }
};

}  // namespace

TEST_P(End2EndBinderTransportTest, SetupTransport) {
  grpc_core::ExecCtx exec_ctx;
  grpc_transport *client_transport, *server_transport;
  std::tie(client_transport, server_transport) =
      end2end_testing::CreateClientServerBindersPairForTesting();
  EXPECT_NE(client_transport, nullptr);
  EXPECT_NE(server_transport, nullptr);

  grpc_transport_destroy(client_transport);
  grpc_transport_destroy(server_transport);
}

TEST_P(End2EndBinderTransportTest, UnaryCall) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCall");
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), "UnaryCall");

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallWithNonOkStatus) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallWithNonOkStatus");
  request.mutable_param()->mutable_expected_error()->set_code(
      grpc::StatusCode::INTERNAL);
  request.mutable_param()->mutable_expected_error()->set_error_message(
      "expected to fail");
  // Server will not response the client with message data, however, since all
  // callbacks after the trailing metadata are cancelled, we shall not be
  // blocked here.
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(), ::testing::HasSubstr("expected to fail"));

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallServerTimeout) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  // std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::shared_ptr<grpc::Channel> channel = server->InProcessChannel(args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallServerTimeout");
  // Server will sleep for 2 seconds before responding us.
  request.mutable_param()->set_server_sleep_us(2000000);
  // Disable cancellation check because the request will time out.
  request.mutable_param()->set_skip_cancelled_check(true);
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallClientTimeout) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  // Set transaction delay to a large number. This happens after the channel
  // creation so that we don't need to wait that long for client and server to
  // be connected.
  end2end_testing::g_transaction_processor->SetDelay(absl::Seconds(5));

  grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallClientTimeout");
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallUnimplemented) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallUnimplemented");
  grpc::Status status = stub->Unimplemented(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallClientCancel) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallClientCancel");
  context.TryCancel();
  grpc::Status status = stub->Unimplemented(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallEchoMetadataInitially) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallEchoMetadataInitially");
  request.mutable_param()->set_echo_metadata_initially(true);
  context.AddMetadata("key1", "value1");
  context.AddMetadata("key2", "value2");
  grpc::Status status = stub->Echo(&context, request, &response);
  const auto& initial_metadata = context.GetServerInitialMetadata();
  EXPECT_EQ(initial_metadata.find("key1")->second, "value1");
  EXPECT_EQ(initial_metadata.find("key2")->second, "value2");

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallEchoMetadata) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallEchoMetadata");
  request.mutable_param()->set_echo_metadata(true);
  context.AddMetadata("key1", "value1");
  context.AddMetadata("key2", "value2");
  grpc::Status status = stub->Echo(&context, request, &response);
  const auto& initial_metadata = context.GetServerTrailingMetadata();
  EXPECT_EQ(initial_metadata.find("key1")->second, "value1");
  EXPECT_EQ(initial_metadata.find("key2")->second, "value2");

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallResponseMessageLength) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  for (size_t response_length : {1, 2, 5, 10, 100, 1000000}) {
    grpc::ClientContext context;
    grpc::testing::EchoRequest request;
    grpc::testing::EchoResponse response;
    request.set_message("UnaryCallResponseMessageLength");
    request.mutable_param()->set_response_message_length(response_length);
    grpc::Status status = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message().length(), response_length);
  }
  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, UnaryCallTryCancel) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  // std::shared_ptr<grpc::Channel> channel = server->InProcessChannel(args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_BEFORE_PROCESSING));
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallTryCancel");
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, ServerStreamingCall) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  constexpr size_t kServerResponseStreamsToSend = 100;
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerResponseStreamsToSend,
                      std::to_string(kServerResponseStreamsToSend));
  grpc::testing::EchoRequest request;
  request.set_message("ServerStreamingCall");
  std::unique_ptr<grpc::ClientReader<grpc::testing::EchoResponse>> reader =
      stub->ResponseStream(&context, request);
  grpc::testing::EchoResponse response;
  size_t cnt = 0;
  while (reader->Read(&response)) {
    EXPECT_EQ(response.message(), "ServerStreamingCall" + std::to_string(cnt));
    cnt++;
  }
  EXPECT_EQ(cnt, kServerResponseStreamsToSend);
  grpc::Status status = reader->Finish();
  EXPECT_TRUE(status.ok());

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, ServerStreamingCallCoalescingApi) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  constexpr size_t kServerResponseStreamsToSend = 100;
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerResponseStreamsToSend,
                      std::to_string(kServerResponseStreamsToSend));
  context.AddMetadata(grpc::testing::kServerUseCoalescingApi, "1");
  grpc::testing::EchoRequest request;
  request.set_message("ServerStreamingCallCoalescingApi");
  std::unique_ptr<grpc::ClientReader<grpc::testing::EchoResponse>> reader =
      stub->ResponseStream(&context, request);
  grpc::testing::EchoResponse response;
  size_t cnt = 0;
  while (reader->Read(&response)) {
    EXPECT_EQ(response.message(),
              "ServerStreamingCallCoalescingApi" + std::to_string(cnt));
    cnt++;
  }
  EXPECT_EQ(cnt, kServerResponseStreamsToSend);
  grpc::Status status = reader->Finish();
  EXPECT_TRUE(status.ok());

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ServerStreamingCallTryCancelBeforeProcessing) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  constexpr size_t kServerResponseStreamsToSend = 100;
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerResponseStreamsToSend,
                      std::to_string(kServerResponseStreamsToSend));
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_BEFORE_PROCESSING));
  grpc::testing::EchoRequest request;
  request.set_message("ServerStreamingCallTryCancelBeforeProcessing");
  std::unique_ptr<grpc::ClientReader<grpc::testing::EchoResponse>> reader =
      stub->ResponseStream(&context, request);
  grpc::testing::EchoResponse response;
  EXPECT_FALSE(reader->Read(&response));
  grpc::Status status = reader->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ServerSteramingCallTryCancelDuringProcessing) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  constexpr size_t kServerResponseStreamsToSend = 2;
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerResponseStreamsToSend,
                      std::to_string(kServerResponseStreamsToSend));
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_DURING_PROCESSING));
  grpc::testing::EchoRequest request;
  request.set_message("ServerStreamingCallTryCancelDuringProcessing");
  std::unique_ptr<grpc::ClientReader<grpc::testing::EchoResponse>> reader =
      stub->ResponseStream(&context, request);
  grpc::testing::EchoResponse response;
  size_t cnt = 0;
  while (reader->Read(&response)) {
    EXPECT_EQ(
        response.message(),
        "ServerStreamingCallTryCancelDuringProcessing" + std::to_string(cnt));
    cnt++;
  }
  grpc::Status status = reader->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ServerSteramingCallTryCancelAfterProcessing) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  constexpr size_t kServerResponseStreamsToSend = 100;
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerResponseStreamsToSend,
                      std::to_string(kServerResponseStreamsToSend));
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_AFTER_PROCESSING));
  grpc::testing::EchoRequest request;
  request.set_message("ServerStreamingCallTryCancelAfterProcessing");
  std::unique_ptr<grpc::ClientReader<grpc::testing::EchoResponse>> reader =
      stub->ResponseStream(&context, request);
  grpc::testing::EchoResponse response;
  size_t cnt = 0;
  while (reader->Read(&response)) {
    EXPECT_EQ(
        response.message(),
        "ServerStreamingCallTryCancelAfterProcessing" + std::to_string(cnt));
    cnt++;
  }
  EXPECT_EQ(cnt, kServerResponseStreamsToSend);
  grpc::Status status = reader->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, ClientStreamingCall) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  grpc::testing::EchoResponse response;
  std::unique_ptr<grpc::ClientWriter<grpc::testing::EchoRequest>> writer =
      stub->RequestStream(&context, &response);
  constexpr size_t kClientStreamingCounts = 100;
  std::string expected = "";
  for (size_t i = 0; i < kClientStreamingCounts; ++i) {
    grpc::testing::EchoRequest request;
    request.set_message("ClientStreamingCall" + std::to_string(i));
    EXPECT_TRUE(writer->Write(request));
    expected += "ClientStreamingCall" + std::to_string(i);
  }
  writer->WritesDone();
  grpc::Status status = writer->Finish();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), expected);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ClientStreamingCallTryCancelBeforeProcessing) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_BEFORE_PROCESSING));
  grpc::testing::EchoResponse response;
  std::unique_ptr<grpc::ClientWriter<grpc::testing::EchoRequest>> writer =
      stub->RequestStream(&context, &response);
  constexpr size_t kClientStreamingCounts = 100;
  for (size_t i = 0; i < kClientStreamingCounts; ++i) {
    grpc::testing::EchoRequest request;
    request.set_message("ClientStreamingCallBeforeProcessing" +
                        std::to_string(i));
    writer->Write(request);
  }
  writer->WritesDone();
  grpc::Status status = writer->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ClientStreamingCallTryCancelDuringProcessing) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_DURING_PROCESSING));
  grpc::testing::EchoResponse response;
  std::unique_ptr<grpc::ClientWriter<grpc::testing::EchoRequest>> writer =
      stub->RequestStream(&context, &response);
  constexpr size_t kClientStreamingCounts = 100;
  for (size_t i = 0; i < kClientStreamingCounts; ++i) {
    grpc::testing::EchoRequest request;
    request.set_message("ClientStreamingCallDuringProcessing" +
                        std::to_string(i));
    writer->Write(request);
  }
  writer->WritesDone();
  grpc::Status status = writer->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ClientStreamingCallTryCancelAfterProcessing) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_AFTER_PROCESSING));
  grpc::testing::EchoResponse response;
  std::unique_ptr<grpc::ClientWriter<grpc::testing::EchoRequest>> writer =
      stub->RequestStream(&context, &response);
  constexpr size_t kClientStreamingCounts = 100;
  for (size_t i = 0; i < kClientStreamingCounts; ++i) {
    grpc::testing::EchoRequest request;
    request.set_message("ClientStreamingCallAfterProcessing" +
                        std::to_string(i));
    writer->Write(request);
  }
  writer->WritesDone();
  grpc::Status status = writer->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, BiDirStreamingCall) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<grpc::testing::EchoRequest,
                                           grpc::testing::EchoResponse>>
      stream = stub->BidiStream(&context);
  constexpr size_t kBiDirStreamingCounts = 100;

  struct WriterArgs {
    std::shared_ptr<grpc::ClientReaderWriter<grpc::testing::EchoRequest,
                                             grpc::testing::EchoResponse>>
        stream;
    size_t bi_dir_streaming_counts;
  } writer_args;

  writer_args.stream = stream;
  writer_args.bi_dir_streaming_counts = kBiDirStreamingCounts;

  auto writer_fn = [](void* arg) {
    const WriterArgs& args = *static_cast<WriterArgs*>(arg);
    for (size_t i = 0; i < args.bi_dir_streaming_counts; ++i) {
      grpc::testing::EchoRequest request;
      request.set_message("BiDirStreamingCall" + std::to_string(i));
      args.stream->Write(request);
    }
    args.stream->WritesDone();
  };

  grpc_core::Thread writer_thread("writer-thread", writer_fn,
                                  static_cast<void*>(&writer_args));
  writer_thread.Start();
  for (size_t i = 0; i < kBiDirStreamingCounts; ++i) {
    grpc::testing::EchoResponse response;
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), "BiDirStreamingCall" + std::to_string(i));
  }
  grpc::Status status = stream->Finish();
  EXPECT_TRUE(status.ok());
  writer_thread.Join();

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, BiDirStreamingCallServerFinishesHalfway) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  grpc::testing::TestServiceImpl service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  constexpr size_t kBiDirStreamingCounts = 100;
  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerFinishAfterNReads,
                      std::to_string(kBiDirStreamingCounts / 2));
  std::shared_ptr<grpc::ClientReaderWriter<grpc::testing::EchoRequest,
                                           grpc::testing::EchoResponse>>
      stream = stub->BidiStream(&context);

  struct WriterArgs {
    std::shared_ptr<grpc::ClientReaderWriter<grpc::testing::EchoRequest,
                                             grpc::testing::EchoResponse>>
        stream;
    size_t bi_dir_streaming_counts;
  } writer_args;

  writer_args.stream = stream;
  writer_args.bi_dir_streaming_counts = kBiDirStreamingCounts;

  auto writer_fn = [](void* arg) {
    const WriterArgs& args = *static_cast<WriterArgs*>(arg);
    for (size_t i = 0; i < args.bi_dir_streaming_counts; ++i) {
      grpc::testing::EchoRequest request;
      request.set_message("BiDirStreamingCallServerFinishesHalfway" +
                          std::to_string(i));
      if (!args.stream->Write(request)) {
        return;
      }
    }
    args.stream->WritesDone();
  };

  grpc_core::Thread writer_thread("writer-thread", writer_fn,
                                  static_cast<void*>(&writer_args));
  writer_thread.Start();
  for (size_t i = 0; i < kBiDirStreamingCounts / 2; ++i) {
    grpc::testing::EchoResponse response;
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(),
              "BiDirStreamingCallServerFinishesHalfway" + std::to_string(i));
  }
  grpc::testing::EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  writer_thread.Join();
  grpc::Status status = stream->Finish();
  EXPECT_TRUE(status.ok());

  server->Shutdown();
}

INSTANTIATE_TEST_SUITE_P(
    End2EndBinderTransportTestWithDifferentDelayTimes,
    End2EndBinderTransportTest,
    testing::Values(absl::ZeroDuration(), absl::Nanoseconds(10),
                    absl::Microseconds(10), absl::Microseconds(100),
                    absl::Milliseconds(1), absl::Milliseconds(20)));

}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}
