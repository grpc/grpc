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

  CompletionQueue cli_cq_;
  CompletionQueue srv_cq_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  grpc::cpp::test::util::TestService::AsyncService service_;
  std::ostringstream server_address_;
};

void* tag(int i) {
  return (void*)(gpr_intptr)i;
}

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

  void *got_tag;
  bool ok;
  EXPECT_TRUE(srv_cq_.Next(&got_tag, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(tag(2), got_tag);
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));

  EXPECT_TRUE(srv_cq_.Next(&got_tag, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(tag(3), got_tag);

  EXPECT_TRUE(cli_cq_.Next(&got_tag, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(tag(1), got_tag);

  EXPECT_EQ(send_response.message(), recv_response.message());
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
