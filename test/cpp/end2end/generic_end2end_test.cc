//
//
// Copyright 2015 gRPC authors.
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

#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/proto_utils.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/slice.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

namespace grpc {
namespace testing {
namespace {

void* tag(int i) { return reinterpret_cast<void*>(i); }

void verify_ok(CompletionQueue* cq, int i, bool expect_ok) {
  bool ok;
  void* got_tag;
  EXPECT_TRUE(cq->Next(&got_tag, &ok));
  EXPECT_EQ(expect_ok, ok);
  EXPECT_EQ(tag(i), got_tag);
}

class GenericEnd2endTest : public ::testing::Test {
 protected:
  GenericEnd2endTest() : server_host_("localhost") {}

  void SetUp() override {
    shut_down_ = false;
    int port = grpc_pick_unused_port_or_die();
    server_address_ << server_host_ << ":" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterAsyncGenericService(&generic_service_);
    // Include a second call to RegisterAsyncGenericService to make sure that
    // we get an error in the log, since it is not allowed to have 2 async
    // generic services
    builder.RegisterAsyncGenericService(&generic_service_);
    srv_cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
  }

  void ShutDownServerAndCQs() {
    if (!shut_down_) {
      server_->Shutdown();
      void* ignored_tag;
      bool ignored_ok;
      cli_cq_.Shutdown();
      srv_cq_->Shutdown();
      while (cli_cq_.Next(&ignored_tag, &ignored_ok)) {
      }
      while (srv_cq_->Next(&ignored_tag, &ignored_ok)) {
      }
      shut_down_ = true;
    }
  }
  void TearDown() override { ShutDownServerAndCQs(); }

  void ResetStub() {
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
    generic_stub_ = std::make_unique<GenericStub>(channel);
  }

  void server_ok(int i) { verify_ok(srv_cq_.get(), i, true); }
  void client_ok(int i) { verify_ok(&cli_cq_, i, true); }
  void server_fail(int i) { verify_ok(srv_cq_.get(), i, false); }
  void client_fail(int i) { verify_ok(&cli_cq_, i, false); }

  void SendRpc(int num_rpcs) {
    SendRpc(num_rpcs, false, gpr_inf_future(GPR_CLOCK_MONOTONIC));
  }

  void SendRpc(int num_rpcs, bool check_deadline, gpr_timespec deadline) {
    const std::string kMethodName("/grpc.cpp.test.util.EchoTestService/Echo");
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest send_request;
      EchoRequest recv_request;
      EchoResponse send_response;
      EchoResponse recv_response;
      Status recv_status;

      ClientContext cli_ctx;
      GenericServerContext srv_ctx;
      GenericServerAsyncReaderWriter stream(&srv_ctx);

      // The string needs to be long enough to test heap-based slice.
      send_request.set_message("Hello world. Hello world. Hello world.");

      if (check_deadline) {
        cli_ctx.set_deadline(deadline);
      }

      // Rather than using the original kMethodName, make a short-lived
      // copy to also confirm that we don't refer to this object beyond
      // the initial call preparation
      const std::string* method_name = new std::string(kMethodName);

      std::unique_ptr<GenericClientAsyncReaderWriter> call =
          generic_stub_->PrepareCall(&cli_ctx, *method_name, &cli_cq_);

      delete method_name;  // Make sure that this is not needed after invocation

      std::thread request_call([this]() { server_ok(4); });
      call->StartCall(tag(1));
      client_ok(1);
      std::unique_ptr<ByteBuffer> send_buffer =
          SerializeToByteBuffer(&send_request);
      call->Write(*send_buffer, tag(2));
      // Send ByteBuffer can be destroyed after calling Write.
      send_buffer.reset();
      client_ok(2);
      call->WritesDone(tag(3));
      client_ok(3);

      generic_service_.RequestCall(&srv_ctx, &stream, srv_cq_.get(),
                                   srv_cq_.get(), tag(4));

      request_call.join();
      EXPECT_EQ(server_host_, srv_ctx.host().substr(0, server_host_.length()));
      EXPECT_EQ(kMethodName, srv_ctx.method());

      if (check_deadline) {
        EXPECT_TRUE(gpr_time_similar(deadline, srv_ctx.raw_deadline(),
                                     gpr_time_from_millis(1000, GPR_TIMESPAN)));
      }

      ByteBuffer recv_buffer;
      stream.Read(&recv_buffer, tag(5));
      server_ok(5);
      EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
      EXPECT_EQ(send_request.message(), recv_request.message());

      send_response.set_message(recv_request.message());
      send_buffer = SerializeToByteBuffer(&send_response);
      stream.Write(*send_buffer, tag(6));
      send_buffer.reset();
      server_ok(6);

      stream.Finish(Status::OK, tag(7));
      server_ok(7);

      recv_buffer.Clear();
      call->Read(&recv_buffer, tag(8));
      client_ok(8);
      EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_response));

      call->Finish(&recv_status, tag(9));
      client_ok(9);

      EXPECT_EQ(send_response.message(), recv_response.message());
      EXPECT_TRUE(recv_status.ok());
    }
  }

  // Return errors to up to one call that comes in on the supplied completion
  // queue, until the CQ is being shut down (and therefore we can no longer
  // enqueue further events).
  void DriveCompletionQueue() {
    enum class Event : uintptr_t {
      kCallReceived,
      kResponseSent,
    };
    // Request the call, but only if the main thread hasn't beaten us to
    // shutting down the CQ.
    grpc::GenericServerContext server_context;
    grpc::GenericServerAsyncReaderWriter reader_writer(&server_context);

    {
      std::lock_guard<std::mutex> lock(shutting_down_mu_);
      if (!shutting_down_) {
        generic_service_.RequestCall(
            &server_context, &reader_writer, srv_cq_.get(), srv_cq_.get(),
            reinterpret_cast<void*>(Event::kCallReceived));
      }
    }
    // Process events.
    {
      Event event;
      bool ok;
      while (srv_cq_->Next(reinterpret_cast<void**>(&event), &ok)) {
        std::lock_guard<std::mutex> lock(shutting_down_mu_);
        if (shutting_down_) {
          // The main thread has started shutting down. Simply continue to drain
          // events.
          continue;
        }

        switch (event) {
          case Event::kCallReceived:
            reader_writer.Finish(
                grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "go away"),
                reinterpret_cast<void*>(Event::kResponseSent));
            break;

          case Event::kResponseSent:
            // We are done.
            break;
        }
      }
    }
  }

  CompletionQueue cli_cq_;
  std::unique_ptr<ServerCompletionQueue> srv_cq_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::GenericStub> generic_stub_;
  std::unique_ptr<Server> server_;
  AsyncGenericService generic_service_;
  const std::string server_host_;
  std::ostringstream server_address_;
  bool shutting_down_;
  bool shut_down_;
  std::mutex shutting_down_mu_;
};

TEST_F(GenericEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(1);
}

TEST_F(GenericEnd2endTest, SequentialRpcs) {
  ResetStub();
  SendRpc(10);
}

TEST_F(GenericEnd2endTest, SequentialUnaryRpcs) {
  ResetStub();
  const int num_rpcs = 10;
  const std::string kMethodName("/grpc.cpp.test.util.EchoTestService/Echo");
  for (int i = 0; i < num_rpcs; i++) {
    EchoRequest send_request;
    EchoRequest recv_request;
    EchoResponse send_response;
    EchoResponse recv_response;
    Status recv_status;

    ClientContext cli_ctx;
    GenericServerContext srv_ctx;
    GenericServerAsyncReaderWriter stream(&srv_ctx);

    // The string needs to be long enough to test heap-based slice.
    send_request.set_message("Hello world. Hello world. Hello world.");

    std::unique_ptr<ByteBuffer> cli_send_buffer =
        SerializeToByteBuffer(&send_request);
    std::thread request_call([this]() { server_ok(4); });
    std::unique_ptr<GenericClientAsyncResponseReader> call =
        generic_stub_->PrepareUnaryCall(&cli_ctx, kMethodName, *cli_send_buffer,
                                        &cli_cq_);
    call->StartCall();
    ByteBuffer cli_recv_buffer;
    call->Finish(&cli_recv_buffer, &recv_status, tag(1));
    std::thread client_check([this] { client_ok(1); });

    generic_service_.RequestCall(&srv_ctx, &stream, srv_cq_.get(),
                                 srv_cq_.get(), tag(4));
    request_call.join();
    EXPECT_EQ(server_host_, srv_ctx.host().substr(0, server_host_.length()));
    EXPECT_EQ(kMethodName, srv_ctx.method());

    ByteBuffer srv_recv_buffer;
    stream.Read(&srv_recv_buffer, tag(5));
    server_ok(5);
    EXPECT_TRUE(ParseFromByteBuffer(&srv_recv_buffer, &recv_request));
    EXPECT_EQ(send_request.message(), recv_request.message());

    send_response.set_message(recv_request.message());
    std::unique_ptr<ByteBuffer> srv_send_buffer =
        SerializeToByteBuffer(&send_response);
    stream.Write(*srv_send_buffer, tag(6));
    server_ok(6);

    stream.Finish(Status::OK, tag(7));
    server_ok(7);

    client_check.join();
    EXPECT_TRUE(ParseFromByteBuffer(&cli_recv_buffer, &recv_response));
    EXPECT_EQ(send_response.message(), recv_response.message());
    EXPECT_TRUE(recv_status.ok());
  }
}

// One ping, one pong.
TEST_F(GenericEnd2endTest, SimpleBidiStreaming) {
  ResetStub();

  const std::string kMethodName(
      "/grpc.cpp.test.util.EchoTestService/BidiStream");
  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  GenericServerContext srv_ctx;
  GenericServerAsyncReaderWriter srv_stream(&srv_ctx);

  cli_ctx.set_compression_algorithm(GRPC_COMPRESS_GZIP);
  send_request.set_message("Hello");
  std::thread request_call([this]() { server_ok(2); });
  std::unique_ptr<GenericClientAsyncReaderWriter> cli_stream =
      generic_stub_->PrepareCall(&cli_ctx, kMethodName, &cli_cq_);
  cli_stream->StartCall(tag(1));
  client_ok(1);

  generic_service_.RequestCall(&srv_ctx, &srv_stream, srv_cq_.get(),
                               srv_cq_.get(), tag(2));
  request_call.join();

  EXPECT_EQ(server_host_, srv_ctx.host().substr(0, server_host_.length()));
  EXPECT_EQ(kMethodName, srv_ctx.method());

  std::unique_ptr<ByteBuffer> send_buffer =
      SerializeToByteBuffer(&send_request);
  cli_stream->Write(*send_buffer, tag(3));
  send_buffer.reset();
  client_ok(3);

  ByteBuffer recv_buffer;
  srv_stream.Read(&recv_buffer, tag(4));
  server_ok(4);
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  send_buffer = SerializeToByteBuffer(&send_response);
  srv_stream.Write(*send_buffer, tag(5));
  send_buffer.reset();
  server_ok(5);

  cli_stream->Read(&recv_buffer, tag(6));
  client_ok(6);
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_response));
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->WritesDone(tag(7));
  client_ok(7);

  srv_stream.Read(&recv_buffer, tag(8));
  server_fail(8);

  srv_stream.Finish(Status::OK, tag(9));
  server_ok(9);

  cli_stream->Finish(&recv_status, tag(10));
  client_ok(10);

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

TEST_F(GenericEnd2endTest, Deadline) {
  ResetStub();
  SendRpc(1, true,
          gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                       gpr_time_from_seconds(10, GPR_TIMESPAN)));
}

TEST_F(GenericEnd2endTest, ShortDeadline) {
  ResetStub();

  ClientContext cli_ctx;
  EchoRequest request;
  EchoResponse response;

  shutting_down_ = false;
  std::thread driver([this] { DriveCompletionQueue(); });

  request.set_message("");
  cli_ctx.set_deadline(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                    gpr_time_from_micros(500, GPR_TIMESPAN)));
  Status s = stub_->Echo(&cli_ctx, request, &response);
  EXPECT_FALSE(s.ok());
  {
    std::lock_guard<std::mutex> lock(shutting_down_mu_);
    shutting_down_ = true;
  }
  ShutDownServerAndCQs();
  driver.join();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
