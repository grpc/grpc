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

#include <string>
#include <thread>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/time/time.h"

#include <grpcpp/grpcpp.h>

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
    service_ = std::make_unique<grpc::testing::TestServiceImpl>();
    grpc::ServerBuilder builder;
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
  }

  ~End2EndBinderTransportTest() override {
    server_->Shutdown();
    service_.reset();
    exec_ctx.Flush();
    delete end2end_testing::g_transaction_processor;
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> NewStub() {
    grpc::ChannelArguments args;
    std::shared_ptr<grpc::Channel> channel = BinderChannel(server_.get(), args);
    return grpc::testing::EchoTestService::NewStub(channel);
  }

  static void SetUpTestSuite() { grpc_init(); }
  static void TearDownTestSuite() { grpc_shutdown(); }

  std::shared_ptr<grpc::Channel> BinderChannel(
      grpc::Server* server, const grpc::ChannelArguments& args) {
    return end2end_testing::BinderChannelForTesting(server, args);
  }

 protected:
  std::unique_ptr<grpc::testing::TestServiceImpl> service_;
  std::unique_ptr<grpc::Server> server_;

 private:
  grpc_core::ExecCtx exec_ctx;
};

}  // namespace

TEST_P(End2EndBinderTransportTest, SetupTransport) {
  grpc_transport *client_transport, *server_transport;
  std::tie(client_transport, server_transport) =
      end2end_testing::CreateClientServerBindersPairForTesting();
  EXPECT_NE(client_transport, nullptr);
  EXPECT_NE(server_transport, nullptr);

  grpc_transport_destroy(client_transport);
  grpc_transport_destroy(server_transport);
}

TEST_P(End2EndBinderTransportTest, UnaryCall) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCall");
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), "UnaryCall");
}

TEST_P(End2EndBinderTransportTest, UnaryCallWithNonOkStatus) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest, UnaryCallServerTimeout) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
  grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(
      absl::Now() + (absl::Seconds(1) * grpc_test_slowdown_factor())));
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
}

TEST_P(End2EndBinderTransportTest, UnaryCallClientTimeout) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();

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
}

TEST_P(End2EndBinderTransportTest, UnaryCallUnimplemented) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();

  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallUnimplemented");
  grpc::Status status = stub->Unimplemented(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED);
}

TEST_P(End2EndBinderTransportTest, UnaryCallClientCancel) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();

  grpc::ClientContext context;
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallClientCancel");
  context.TryCancel();
  grpc::Status status = stub->Unimplemented(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

TEST_P(End2EndBinderTransportTest, UnaryCallEchoMetadataInitially) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();

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
}

TEST_P(End2EndBinderTransportTest, UnaryCallEchoMetadata) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();

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
}

TEST_P(End2EndBinderTransportTest, UnaryCallResponseMessageLength) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();

  for (size_t response_length : {1, 2, 5, 10, 100, 1000000}) {
    grpc::ClientContext context;
    grpc::testing::EchoRequest request;
    grpc::testing::EchoResponse response;
    request.set_message("UnaryCallResponseMessageLength");
    request.mutable_param()->set_response_message_length(response_length);
    grpc::Status status = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message().length(), response_length);
  }
}

TEST_P(End2EndBinderTransportTest, UnaryCallTryCancel) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();

  grpc::ClientContext context;
  context.AddMetadata(grpc::testing::kServerTryCancelRequest,
                      std::to_string(grpc::testing::CANCEL_BEFORE_PROCESSING));
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message("UnaryCallTryCancel");
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

TEST_P(End2EndBinderTransportTest, ServerStreamingCall) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest, ServerStreamingCallCoalescingApi) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest,
       ServerStreamingCallTryCancelBeforeProcessing) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest,
       ServerSteramingCallTryCancelDuringProcessing) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest,
       ServerSteramingCallTryCancelAfterProcessing) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest, ClientStreamingCall) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
  grpc::ClientContext context;
  grpc::testing::EchoResponse response;
  std::unique_ptr<grpc::ClientWriter<grpc::testing::EchoRequest>> writer =
      stub->RequestStream(&context, &response);
  constexpr size_t kClientStreamingCounts = 100;
  std::string expected;
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
}

TEST_P(End2EndBinderTransportTest,
       ClientStreamingCallTryCancelBeforeProcessing) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest,
       ClientStreamingCallTryCancelDuringProcessing) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest,
       ClientStreamingCallTryCancelAfterProcessing) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest, BiDirStreamingCall) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest, BiDirStreamingCallServerFinishesHalfway) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
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
}

TEST_P(End2EndBinderTransportTest, LargeMessages) {
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub = NewStub();
  for (size_t size = 1; size <= 1024 * 1024; size *= 4) {
    grpc::ClientContext context;
    grpc::testing::EchoRequest request;
    grpc::testing::EchoResponse response;
    request.set_message(std::string(size, 'a'));
    grpc::Status status = stub->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.message().size(), size);
    EXPECT_TRUE(std::all_of(response.message().begin(),
                            response.message().end(),
                            [](char c) { return c == 'a'; }));
  }
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
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
