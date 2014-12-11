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


#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <google/gflags.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/cpp/util/test_ssl_channel.h"
#include "test/cpp/interop/test.pb.h"
#include "test/cpp/interop/empty.pb.h"
#include "test/cpp/interop/messages.pb.h"

DEFINE_bool(enable_ssl, false, "Whether to use ssl/tls.");
DEFINE_int32(server_port, 0, "Server port.");
DEFINE_string(server_host, "127.0.0.1", "Server host.");
DEFINE_string(test_case, "large_unary",
              "Configure different test cases. Valid options are: "
              "empty_unary : empty (zero bytes) request and response; "
              "large_unary : single request and (large) response; "
              "client_streaming : request streaming with single response; "
              "server_streaming : single request with response streaming; "
              "half_duplex : half-duplex streaming;"
              "ping_pong : full-duplex streaming;"
              "all : all of above.");

using grpc::ChannelInterface;
using grpc::ClientContext;
using grpc::CreateChannel;
using grpc::TestSslChannel;
using grpc::testing::ResponseParameters;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StreamingInputCallRequest;
using grpc::testing::StreamingInputCallResponse;
using grpc::testing::StreamingOutputCallRequest;
using grpc::testing::StreamingOutputCallResponse;
using grpc::testing::TestService;

namespace {
// The same value is defined by the Java client.
const std::vector<int> request_stream_sizes = {27182, 8, 1828, 45904};
const std::vector<int> response_stream_sizes = {31415, 9, 2653, 58979};
}  // namespace

std::shared_ptr<ChannelInterface> CreateTestChannel(
    const grpc::string& server) {
  std::shared_ptr<ChannelInterface> channel;
  if (FLAGS_enable_ssl) {
    channel.reset(new TestSslChannel(server));
  } else {
    channel = CreateChannel(server);
  }
  return channel;
}

void DoEmpty(std::shared_ptr<ChannelInterface> channel) {
  gpr_log(GPR_INFO, "Sending an empty rpc...");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  grpc::testing::Empty request = grpc::testing::Empty::default_instance();
  grpc::testing::Empty response = grpc::testing::Empty::default_instance();
  ClientContext context;

  grpc::Status s = stub->EmptyCall(&context, request, &response);

  GPR_ASSERT(s.IsOk());
  gpr_log(GPR_INFO, "Empty rpc done.");
}

void DoLargeUnary(std::shared_ptr<ChannelInterface> channel) {
  gpr_log(GPR_INFO, "Sending a large unary rpc...");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  SimpleRequest request;
  SimpleResponse response;
  ClientContext context;
  request.set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
  request.set_response_size(314159);
  grpc::string payload(271828, '0');
  request.mutable_payload()->set_body(payload.c_str(), 271828);

  grpc::Status s = stub->UnaryCall(&context, request, &response);

  GPR_ASSERT(s.IsOk());
  GPR_ASSERT(response.payload().type() ==
         grpc::testing::PayloadType::COMPRESSABLE);
  GPR_ASSERT(response.payload().body().length() == 314159);
  gpr_log(GPR_INFO, "Large unary done.");
}


int main(int argc, char** argv) {
  grpc_init();

  google::ParseCommandLineFlags(&argc, &argv, true);

  GPR_ASSERT(FLAGS_server_port);
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", FLAGS_server_host.c_str(),
           FLAGS_server_port);

  if (FLAGS_test_case == "empty_unary") {
    DoEmpty(CreateTestChannel(host_port));
  } else if (FLAGS_test_case == "large_unary") {
    DoLargeUnary(CreateTestChannel(host_port));
  } else {
    gpr_log(
        GPR_ERROR,
        "Unsupported test case %s. Valid options are all|empty_unary|"
        "large_unary|client_streaming|server_streaming|half_duplex|ping_pong",
        FLAGS_test_case.c_str());
  }

  grpc_shutdown();
  return 0;
}
