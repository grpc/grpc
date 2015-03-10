/*
 *
 * Copyright 2015, Google Inc.
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
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <gflags/gflags.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/interop/test.pb.h"
#include "test/cpp/interop/empty.pb.h"
#include "test/cpp/interop/messages.pb.h"

DEFINE_bool(enable_ssl, false, "Whether to use ssl/tls.");
DEFINE_bool(use_prod_roots, false, "True to use SSL roots for google");
DEFINE_int32(server_port, 0, "Server port.");
DEFINE_string(server_host, "127.0.0.1", "Server host to connect to");
DEFINE_string(server_host_override, "foo.test.google.fr",
              "Override the server host which is sent in HTTP header");
DEFINE_string(test_case, "large_unary",
              "Configure different test cases. Valid options are: "
              "empty_unary : empty (zero bytes) request and response; "
              "large_unary : single request and (large) response; "
              "client_streaming : request streaming with single response; "
              "server_streaming : single request with response streaming; "
              "slow_consumer : single request with response; "
              " streaming with slow client consumer; "
              "half_duplex : half-duplex streaming; "
              "ping_pong : full-duplex streaming; "
              "service_account_creds : large_unary with service_account auth; "
              "compute_engine_creds: large_unary with compute engine auth; "
              "all : all of above.");
DEFINE_string(default_service_account, "",
              "Email of GCE default service account");
DEFINE_string(service_account_key_file, "",
              "Path to service account json key file.");
DEFINE_string(oauth_scope, "", "Scope for OAuth tokens.");

using grpc::ChannelInterface;
using grpc::ClientContext;
using grpc::ComputeEngineCredentials;
using grpc::CreateTestChannel;
using grpc::Credentials;
using grpc::ServiceAccountCredentials;
using grpc::testing::ResponseParameters;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StreamingInputCallRequest;
using grpc::testing::StreamingInputCallResponse;
using grpc::testing::StreamingOutputCallRequest;
using grpc::testing::StreamingOutputCallResponse;
using grpc::testing::TestService;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

namespace {
// The same value is defined by the Java client.
const std::vector<int> request_stream_sizes = {27182, 8, 1828, 45904};
const std::vector<int> response_stream_sizes = {31415, 9, 2653, 58979};
const int kNumResponseMessages = 2000;
const int kResponseMessageSize = 1030;
const int kReceiveDelayMilliSeconds = 20;
const int kLargeRequestSize = 314159;
const int kLargeResponseSize = 271812;
}  // namespace

grpc::string GetServiceAccountJsonKey() {
  static grpc::string json_key;
  if (json_key.empty()) {
    std::ifstream json_key_file(FLAGS_service_account_key_file);
    std::stringstream key_stream;
    key_stream << json_key_file.rdbuf();
    json_key = key_stream.str();
  }
  return json_key;
}

std::shared_ptr<ChannelInterface> CreateChannelForTestCase(
    const grpc::string& test_case) {
  GPR_ASSERT(FLAGS_server_port);
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", FLAGS_server_host.c_str(),
           FLAGS_server_port);

  if (test_case == "service_account_creds") {
    std::unique_ptr<Credentials> creds;
    GPR_ASSERT(FLAGS_enable_ssl);
    grpc::string json_key = GetServiceAccountJsonKey();
    creds = ServiceAccountCredentials(json_key, FLAGS_oauth_scope,
                                      std::chrono::hours(1));
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_enable_ssl, FLAGS_use_prod_roots, creds);
  } else if (test_case == "compute_engine_creds") {
    std::unique_ptr<Credentials> creds;
    GPR_ASSERT(FLAGS_enable_ssl);
    creds = ComputeEngineCredentials();
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_enable_ssl, FLAGS_use_prod_roots, creds);
  } else {
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_enable_ssl, FLAGS_use_prod_roots);
  }
}

void DoEmpty() {
  gpr_log(GPR_INFO, "Sending an empty rpc...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("empty_unary");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  grpc::testing::Empty request = grpc::testing::Empty::default_instance();
  grpc::testing::Empty response = grpc::testing::Empty::default_instance();
  ClientContext context;

  grpc::Status s = stub->EmptyCall(&context, request, &response);

  GPR_ASSERT(s.IsOk());
  gpr_log(GPR_INFO, "Empty rpc done.");
}

// Shared code to set large payload, make rpc and check response payload.
void PerformLargeUnary(std::shared_ptr<ChannelInterface> channel,
                       SimpleRequest* request, SimpleResponse* response) {
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  ClientContext context;
  request->set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
  request->set_response_size(kLargeResponseSize);
  grpc::string payload(kLargeRequestSize, '\0');
  request->mutable_payload()->set_body(payload.c_str(), kLargeRequestSize);

  grpc::Status s = stub->UnaryCall(&context, *request, response);

  GPR_ASSERT(s.IsOk());
  GPR_ASSERT(response->payload().type() ==
             grpc::testing::PayloadType::COMPRESSABLE);
  GPR_ASSERT(response->payload().body() ==
             grpc::string(kLargeResponseSize, '\0'));
}

void DoComputeEngineCreds() {
  gpr_log(GPR_INFO,
          "Sending a large unary rpc with compute engine credentials ...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("compute_engine_creds");
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);
  request.set_fill_oauth_scope(true);
  PerformLargeUnary(channel, &request, &response);
  gpr_log(GPR_INFO, "Got username %s", response.username().c_str());
  gpr_log(GPR_INFO, "Got oauth_scope %s", response.oauth_scope().c_str());
  GPR_ASSERT(!response.username().empty());
  GPR_ASSERT(response.username().c_str() == FLAGS_default_service_account);
  GPR_ASSERT(!response.oauth_scope().empty());
  const char* oauth_scope_str = response.oauth_scope().c_str();
  GPR_ASSERT(FLAGS_oauth_scope.find(oauth_scope_str) != grpc::string::npos);
  gpr_log(GPR_INFO, "Large unary with compute engine creds done.");
}

void DoServiceAccountCreds() {
  gpr_log(GPR_INFO,
          "Sending a large unary rpc with service account credentials ...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("service_account_creds");
  SimpleRequest request;
  SimpleResponse response;
  request.set_fill_username(true);
  request.set_fill_oauth_scope(true);
  PerformLargeUnary(channel, &request, &response);
  GPR_ASSERT(!response.username().empty());
  GPR_ASSERT(!response.oauth_scope().empty());
  grpc::string json_key = GetServiceAccountJsonKey();
  GPR_ASSERT(json_key.find(response.username()) != grpc::string::npos);
  const char* oauth_scope_str = response.oauth_scope().c_str();
  GPR_ASSERT(FLAGS_oauth_scope.find(oauth_scope_str) != grpc::string::npos);
  gpr_log(GPR_INFO, "Large unary with service account creds done.");
}

void DoLargeUnary() {
  gpr_log(GPR_INFO, "Sending a large unary rpc...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("large_unary");
  SimpleRequest request;
  SimpleResponse response;
  PerformLargeUnary(channel, &request, &response);
  gpr_log(GPR_INFO, "Large unary done.");
}

void DoRequestStreaming() {
  gpr_log(GPR_INFO, "Sending request steaming rpc ...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("client_streaming");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  grpc::ClientContext context;
  StreamingInputCallRequest request;
  StreamingInputCallResponse response;

  std::unique_ptr<grpc::ClientWriter<StreamingInputCallRequest>> stream(
      stub->StreamingInputCall(&context, &response));

  int aggregated_payload_size = 0;
  for (unsigned int i = 0; i < request_stream_sizes.size(); ++i) {
    grpc::testing::Payload* payload = request.mutable_payload();
    payload->set_body(grpc::string(request_stream_sizes[i], '\0'));
    GPR_ASSERT(stream->Write(request));
    aggregated_payload_size += request_stream_sizes[i];
  }
  stream->WritesDone();
  grpc::Status s = stream->Finish();

  GPR_ASSERT(response.aggregated_payload_size() == aggregated_payload_size);
  GPR_ASSERT(s.IsOk());
  gpr_log(GPR_INFO, "Request streaming done.");
}

void DoResponseStreaming() {
  gpr_log(GPR_INFO, "Receiving response steaming rpc ...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("server_streaming");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  grpc::ClientContext context;
  StreamingOutputCallRequest request;
  for (unsigned int i = 0; i < response_stream_sizes.size(); ++i) {
    ResponseParameters* response_parameter = request.add_response_parameters();
    response_parameter->set_size(response_stream_sizes[i]);
  }
  StreamingOutputCallResponse response;
  std::unique_ptr<grpc::ClientReader<StreamingOutputCallResponse>> stream(
      stub->StreamingOutputCall(&context, request));

  unsigned int i = 0;
  while (stream->Read(&response)) {
    GPR_ASSERT(response.payload().body() ==
               grpc::string(response_stream_sizes[i], '\0'));
    ++i;
  }
  GPR_ASSERT(response_stream_sizes.size() == i);
  grpc::Status s = stream->Finish();

  GPR_ASSERT(s.IsOk());
  gpr_log(GPR_INFO, "Response streaming done.");
}

void DoResponseStreamingWithSlowConsumer() {
  gpr_log(GPR_INFO, "Receiving response steaming rpc with slow consumer ...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("slow_consumer");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  grpc::ClientContext context;
  StreamingOutputCallRequest request;

  for (int i = 0; i < kNumResponseMessages; ++i) {
    ResponseParameters* response_parameter = request.add_response_parameters();
    response_parameter->set_size(kResponseMessageSize);
  }
  StreamingOutputCallResponse response;
  std::unique_ptr<grpc::ClientReader<StreamingOutputCallResponse>> stream(
      stub->StreamingOutputCall(&context, request));

  int i = 0;
  while (stream->Read(&response)) {
    GPR_ASSERT(response.payload().body() ==
               grpc::string(kResponseMessageSize, '\0'));
    gpr_log(GPR_INFO, "received message %d", i);
    usleep(kReceiveDelayMilliSeconds * 1000);
    ++i;
  }
  GPR_ASSERT(kNumResponseMessages == i);
  grpc::Status s = stream->Finish();

  GPR_ASSERT(s.IsOk());
  gpr_log(GPR_INFO, "Response streaming done.");
}

void DoHalfDuplex() {
  gpr_log(GPR_INFO, "Sending half-duplex streaming rpc ...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("half_duplex");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReaderWriter<StreamingOutputCallRequest,
                                           StreamingOutputCallResponse>>
      stream(stub->HalfDuplexCall(&context));

  StreamingOutputCallRequest request;
  ResponseParameters* response_parameter = request.add_response_parameters();
  for (unsigned int i = 0; i < response_stream_sizes.size(); ++i) {
    response_parameter->set_size(response_stream_sizes[i]);
    GPR_ASSERT(stream->Write(request));
  }
  stream->WritesDone();

  unsigned int i = 0;
  StreamingOutputCallResponse response;
  while (stream->Read(&response)) {
    GPR_ASSERT(response.payload().has_body());
    GPR_ASSERT(response.payload().body() ==
               grpc::string(response_stream_sizes[i], '\0'));
    ++i;
  }
  GPR_ASSERT(response_stream_sizes.size() == i);
  grpc::Status s = stream->Finish();
  GPR_ASSERT(s.IsOk());
  gpr_log(GPR_INFO, "Half-duplex streaming rpc done.");
}

void DoPingPong() {
  gpr_log(GPR_INFO, "Sending Ping Pong streaming rpc ...");
  std::shared_ptr<ChannelInterface> channel =
      CreateChannelForTestCase("ping_pong");
  std::unique_ptr<TestService::Stub> stub(TestService::NewStub(channel));

  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReaderWriter<StreamingOutputCallRequest,
                                           StreamingOutputCallResponse>>
      stream(stub->FullDuplexCall(&context));

  StreamingOutputCallRequest request;
  request.set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
  ResponseParameters* response_parameter = request.add_response_parameters();
  grpc::testing::Payload* payload = request.mutable_payload();
  StreamingOutputCallResponse response;
  for (unsigned int i = 0; i < request_stream_sizes.size(); ++i) {
    response_parameter->set_size(response_stream_sizes[i]);
    payload->set_body(grpc::string(request_stream_sizes[i], '\0'));
    GPR_ASSERT(stream->Write(request));
    GPR_ASSERT(stream->Read(&response));
    GPR_ASSERT(response.payload().has_body());
    GPR_ASSERT(response.payload().body() ==
               grpc::string(response_stream_sizes[i], '\0'));
  }

  stream->WritesDone();
  GPR_ASSERT(!stream->Read(&response));
  grpc::Status s = stream->Finish();
  GPR_ASSERT(s.IsOk());
  gpr_log(GPR_INFO, "Ping pong streaming done.");
}

int main(int argc, char** argv) {
  grpc_init();

  ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_test_case == "empty_unary") {
    DoEmpty();
  } else if (FLAGS_test_case == "large_unary") {
    DoLargeUnary();
  } else if (FLAGS_test_case == "client_streaming") {
    DoRequestStreaming();
  } else if (FLAGS_test_case == "server_streaming") {
    DoResponseStreaming();
  } else if (FLAGS_test_case == "slow_consumer") {
    DoResponseStreamingWithSlowConsumer();
  } else if (FLAGS_test_case == "half_duplex") {
    DoHalfDuplex();
  } else if (FLAGS_test_case == "ping_pong") {
    DoPingPong();
  } else if (FLAGS_test_case == "service_account_creds") {
    DoServiceAccountCreds();
  } else if (FLAGS_test_case == "compute_engine_creds") {
    DoComputeEngineCreds();
  } else if (FLAGS_test_case == "all") {
    DoEmpty();
    DoLargeUnary();
    DoRequestStreaming();
    DoResponseStreaming();
    DoHalfDuplex();
    DoPingPong();
    // service_account_creds can only run with ssl.
    if (FLAGS_enable_ssl) {
      DoServiceAccountCreds();
    }
    // compute_engine_creds only runs in GCE.
  } else {
    gpr_log(
        GPR_ERROR,
        "Unsupported test case %s. Valid options are all|empty_unary|"
        "large_unary|client_streaming|server_streaming|half_duplex|ping_pong|"
        "service_account_creds|compute_engine_creds",
        FLAGS_test_case.c_str());
  }

  grpc_shutdown();
  return 0;
}
