/*
 *
 * Copyright 2014, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <chrono>
#include <thread>

#include "test/core/util/test_config.h"
#include "test/cpp/util/echo_duplicate.pb.h"
#include "test/cpp/util/echo.pb.h"
#include "src/cpp/util/time.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/core/util/port.h"
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;
using std::chrono::system_clock;

namespace grpc {
namespace testing {

namespace {

void* tag(int i) {
  return (void*)(gpr_intptr)i;
}

void verify_ok(CompletionQueue* cq, int i, bool expect_ok) {
  bool ok;
  void* got_tag;
  EXPECT_TRUE(cq->Next(&got_tag, &ok));
  EXPECT_EQ(expect_ok, ok);
  EXPECT_EQ(tag(i), got_tag);
}

class End2endTest : public ::testing::Test {
 protected:
  End2endTest() : service_(&srv_cq_) {}

  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddPort(server_address_.str());
    builder.RegisterAsyncService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() override { server_->Shutdown(); }

  void ResetStub() {
    std::shared_ptr<ChannelInterface> channel =
        CreateChannel(server_address_.str(), ChannelArguments());
    stub_.reset(grpc::cpp::test::util::TestService::NewStub(channel));
  }

  void server_ok(int i) {
    verify_ok(&srv_cq_, i, true);
  }
  void client_ok(int i) {
    verify_ok(&cli_cq_, i , true);
  }
  void server_fail(int i) {
    verify_ok(&srv_cq_, i, false);
  }
  void client_fail(int i) {
    verify_ok(&cli_cq_, i, false);
  }

  CompletionQueue cli_cq_;
  CompletionQueue srv_cq_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  grpc::cpp::test::util::TestService::AsyncService service_;
  std::ostringstream server_address_;
};

TEST_F(End2endTest, SimpleRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message("Hello");
  stub_->Echo(
      &cli_ctx, send_request, &recv_response, &recv_status, &cli_cq_, tag(1));

  service_.RequestEcho(
      &srv_ctx, &recv_request, &response_writer, &srv_cq_, tag(2));

  server_ok(2);
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));

  server_ok(3);

  client_ok(1);

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.IsOk());
}

// Two pings and a final pong.
TEST_F(End2endTest, SimpleClientStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReader<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message("Hello");
  ClientAsyncWriter<EchoRequest>* cli_stream =
      stub_->RequestStream(&cli_ctx, &recv_response, &cli_cq_, tag(1));

  service_.RequestRequestStream(
      &srv_ctx, &srv_stream, &srv_cq_, tag(2));

  server_ok(2);
  client_ok(1);

  cli_stream->Write(send_request, tag(3));
  client_ok(3);

  srv_stream.Read(&recv_request, tag(4));
  server_ok(4);
  EXPECT_EQ(send_request.message(), recv_request.message());

  cli_stream->Write(send_request, tag(5));
  client_ok(5);

  srv_stream.Read(&recv_request, tag(6));
  server_ok(6);

  EXPECT_EQ(send_request.message(), recv_request.message());
  cli_stream->WritesDone(tag(7));
  client_ok(7);

  srv_stream.Read(&recv_request, tag(8));
  server_fail(8);

  send_response.set_message(recv_request.message());
  srv_stream.Finish(send_response, Status::OK, tag(9));
  server_ok(9);

  cli_stream->Finish(&recv_status, tag(10));
  client_ok(10);

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.IsOk());
}

// One ping, two pongs.
TEST_F(End2endTest, SimpleServerStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncWriter<EchoResponse> srv_stream(&srv_ctx);

  send_request.set_message("Hello");
  ClientAsyncReader<EchoResponse>* cli_stream =
      stub_->ResponseStream(&cli_ctx, send_request, &cli_cq_, tag(1));

  service_.RequestResponseStream(
      &srv_ctx, &recv_request, &srv_stream, &srv_cq_, tag(2));

  server_ok(2);
  client_ok(1);
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(3));
  server_ok(3);

  cli_stream->Read(&recv_response, tag(4));
  client_ok(4);
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.Write(send_response, tag(5));
  server_ok(5);

  cli_stream->Read(&recv_response, tag(6));
  client_ok(6);
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.Finish(Status::OK, tag(7));
  server_ok(7);

  cli_stream->Read(&recv_response, tag(8));
  client_fail(8);

  cli_stream->Finish(&recv_status, tag(9));
  client_ok(9);

  EXPECT_TRUE(recv_status.IsOk());
}

// One ping, one pong.
TEST_F(End2endTest, SimpleBidiStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReaderWriter<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message("Hello");
  ClientAsyncReaderWriter<EchoRequest, EchoResponse>* cli_stream =
      stub_->BidiStream(&cli_ctx, &cli_cq_, tag(1));

  service_.RequestBidiStream(
      &srv_ctx, &srv_stream, &srv_cq_, tag(2));

  server_ok(2);
  client_ok(1);

  cli_stream->Write(send_request, tag(3));
  client_ok(3);

  srv_stream.Read(&recv_request, tag(4));
  server_ok(4);
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(5));
  server_ok(5);

  cli_stream->Read(&recv_response, tag(6));
  client_ok(6);
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->WritesDone(tag(7));
  client_ok(7);

  srv_stream.Read(&recv_request, tag(8));
  server_fail(8);

  srv_stream.Finish(Status::OK, tag(9));
  server_ok(9);

  cli_stream->Finish(&recv_status, tag(10));
  client_ok(10);

  EXPECT_TRUE(recv_status.IsOk());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  google::protobuf::ShutdownProtobufLibrary();
  return result;
}
