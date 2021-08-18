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
#include "test/core/transport/binder/end2end/echo_service.h"
#include "test/core/transport/binder/end2end/fake_binder.h"
#include "test/core/transport/binder/end2end/testing_channel_create.h"
#include "test/core/util/test_config.h"

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

using end2end_testing::EchoRequest;
using end2end_testing::EchoResponse;
using end2end_testing::EchoService;

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

TEST_P(End2EndBinderTransportTest, UnaryCallThroughFakeBinderChannel) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);
  grpc::ClientContext context;
  EchoRequest request;
  EchoResponse response;
  request.set_text("it works!");
  grpc::Status status = stub->EchoUnaryCall(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.text(), "it works!");

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       UnaryCallThroughFakeBinderChannelNonOkStatus) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);
  grpc::ClientContext context;
  EchoRequest request;
  EchoResponse response;
  request.set_text(std::string(end2end_testing::EchoServer::kCancelledText));
  // Server will not response the client with message data, however, since all
  // callbacks after the trailing metadata are cancelled, we shall not be
  // blocked here.
  grpc::Status status = stub->EchoUnaryCall(&context, request, &response);
  EXPECT_FALSE(status.ok());

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       UnaryCallThroughFakeBinderChannelServerTimeout) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);
  grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
  EchoRequest request;
  EchoResponse response;
  request.set_text(std::string(end2end_testing::EchoServer::kTimeoutText));
  grpc::Status status = stub->EchoUnaryCall(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "Deadline Exceeded");

  server->Shutdown();
}

// Temporarily disabled due to a potential deadlock in our design.
// TODO(waynetu): Enable this test once the issue is resolved.
TEST_P(End2EndBinderTransportTest,
       UnaryCallThroughFakeBinderChannelClientTimeout) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);

  // Set transaction delay to a large number. This happens after the channel
  // creation so that we don't need to wait that long for client and server to
  // be connected.
  end2end_testing::g_transaction_processor->SetDelay(absl::Seconds(5));

  grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
  EchoRequest request;
  EchoResponse response;
  request.set_text("normal-text");
  grpc::Status status = stub->EchoUnaryCall(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "Deadline Exceeded");

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ServerStreamingCallThroughFakeBinderChannel) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);
  grpc::ClientContext context;
  EchoRequest request;
  request.set_text("it works!");
  std::unique_ptr<grpc::ClientReader<EchoResponse>> reader =
      stub->EchoServerStreamingCall(&context, request);
  EchoResponse response;
  size_t cnt = 0;
  while (reader->Read(&response)) {
    EXPECT_EQ(response.text(), absl::StrFormat("it works!(%d)", cnt));
    cnt++;
  }
  EXPECT_EQ(cnt, end2end_testing::EchoServer::kServerStreamingCounts);
  grpc::Status status = reader->Finish();
  EXPECT_TRUE(status.ok());

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ServerStreamingCallThroughFakeBinderChannelServerTimeout) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);
  grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
  EchoRequest request;
  request.set_text(std::string(end2end_testing::EchoServer::kTimeoutText));
  std::unique_ptr<grpc::ClientReader<EchoResponse>> reader =
      stub->EchoServerStreamingCall(&context, request);
  EchoResponse response;
  EXPECT_FALSE(reader->Read(&response));
  grpc::Status status = reader->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_message(), "Deadline Exceeded");

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest,
       ClientStreamingCallThroughFakeBinderChannel) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);
  grpc::ClientContext context;
  EchoResponse response;
  std::unique_ptr<grpc::ClientWriter<EchoRequest>> writer =
      stub->EchoClientStreamingCall(&context, &response);
  constexpr size_t kClientStreamingCounts = 100;
  std::string expected = "";
  for (size_t i = 0; i < kClientStreamingCounts; ++i) {
    EchoRequest request;
    request.set_text(absl::StrFormat("it works!(%d)", i));
    writer->Write(request);
    expected += absl::StrFormat("it works!(%d)", i);
  }
  writer->WritesDone();
  grpc::Status status = writer->Finish();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.text(), expected);

  server->Shutdown();
}

TEST_P(End2EndBinderTransportTest, BiDirStreamingCallThroughFakeBinderChannel) {
  grpc::ChannelArguments args;
  grpc::ServerBuilder builder;
  end2end_testing::EchoServer service;
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  std::shared_ptr<grpc::Channel> channel = BinderChannel(server.get(), args);
  std::unique_ptr<EchoService::Stub> stub = EchoService::NewStub(channel);
  grpc::ClientContext context;
  EchoResponse response;
  std::shared_ptr<grpc::ClientReaderWriter<EchoRequest, EchoResponse>> stream =
      stub->EchoBiDirStreamingCall(&context);
  constexpr size_t kBiDirStreamingCounts = 100;

  struct WriterArgs {
    std::shared_ptr<grpc::ClientReaderWriter<EchoRequest, EchoResponse>> stream;
    size_t bi_dir_streaming_counts;
  } writer_args;

  writer_args.stream = stream;
  writer_args.bi_dir_streaming_counts = kBiDirStreamingCounts;

  auto writer_fn = [](void* arg) {
    const WriterArgs& args = *static_cast<WriterArgs*>(arg);
    EchoResponse response;
    for (size_t i = 0; i < args.bi_dir_streaming_counts; ++i) {
      EchoRequest request;
      request.set_text(absl::StrFormat("it works!(%d)", i));
      args.stream->Write(request);
    }
    args.stream->WritesDone();
  };

  grpc_core::Thread writer_thread("writer-thread", writer_fn,
                                  static_cast<void*>(&writer_args));
  writer_thread.Start();
  for (size_t i = 0; i < kBiDirStreamingCounts; ++i) {
    EchoResponse response;
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.text(), absl::StrFormat("it works!(%d)", i));
  }
  grpc::Status status = stream->Finish();
  EXPECT_TRUE(status.ok());
  writer_thread.Join();

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
