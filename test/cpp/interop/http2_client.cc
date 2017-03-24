/*
 *
 * Copyright 2016, Google Inc.
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

#include <thread>

#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/transport/byte_stream.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/interop/http2_client.h"

#include "src/core/lib/support/string.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

namespace {
const int kLargeRequestSize = 271828;
const int kLargeResponseSize = 314159;
}  // namespace

Http2Client::ServiceStub::ServiceStub(std::shared_ptr<Channel> channel)
    : channel_(channel) {
  stub_ = TestService::NewStub(channel);
}

TestService::Stub* Http2Client::ServiceStub::Get() { return stub_.get(); }

Http2Client::Http2Client(std::shared_ptr<Channel> channel)
    : serviceStub_(channel),
      channel_(channel),
      defaultRequest_(BuildDefaultRequest()) {}

bool Http2Client::AssertStatusCode(const Status& s, StatusCode expected_code) {
  if (s.error_code() == expected_code) {
    return true;
  }

  gpr_log(GPR_ERROR, "Error status code: %d (expected: %d), message: %s",
          s.error_code(), expected_code, s.error_message().c_str());
  abort();
}

Status Http2Client::SendUnaryCall(SimpleResponse* response) {
  ClientContext context;
  return serviceStub_.Get()->UnaryCall(&context, defaultRequest_, response);
}

SimpleRequest Http2Client::BuildDefaultRequest() {
  SimpleRequest request;
  request.set_response_size(kLargeResponseSize);
  grpc::string payload(kLargeRequestSize, '\0');
  request.mutable_payload()->set_body(payload.c_str(), kLargeRequestSize);
  return request;
}

bool Http2Client::DoRstAfterHeader() {
  gpr_log(GPR_DEBUG, "Sending RPC and expecting reset stream after header");

  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::INTERNAL);
  GPR_ASSERT(!response.has_payload());  // no data should be received

  gpr_log(GPR_DEBUG, "Done testing reset stream after header");
  return true;
}

bool Http2Client::DoRstAfterData() {
  gpr_log(GPR_DEBUG, "Sending RPC and expecting reset stream after data");

  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::INTERNAL);
  GPR_ASSERT(response.has_payload());  // data should be received

  gpr_log(GPR_DEBUG, "Done testing reset stream after data");
  return true;
}

bool Http2Client::DoRstDuringData() {
  gpr_log(GPR_DEBUG, "Sending RPC and expecting reset stream during data");

  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::INTERNAL);
  GPR_ASSERT(!response.has_payload());  // no data should be received

  gpr_log(GPR_DEBUG, "Done testing reset stream during data");
  return true;
}

bool Http2Client::DoGoaway() {
  gpr_log(GPR_DEBUG, "Sending two RPCs and expecting goaway");
  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             grpc::string(kLargeResponseSize, '\0'));

  // Sleep for one second to give time for client to receive goaway frame.
  gpr_timespec sleep_time = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(1, GPR_TIMESPAN));
  gpr_sleep_until(sleep_time);

  response.Clear();
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             grpc::string(kLargeResponseSize, '\0'));
  gpr_log(GPR_DEBUG, "Done testing goaway");
  return true;
}

bool Http2Client::DoPing() {
  gpr_log(GPR_DEBUG, "Sending RPC and expecting ping");
  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             grpc::string(kLargeResponseSize, '\0'));
  gpr_log(GPR_DEBUG, "Done testing ping");
  return true;
}

void Http2Client::MaxStreamsWorker(std::shared_ptr<grpc::Channel> channel) {
  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             grpc::string(kLargeResponseSize, '\0'));
}

bool Http2Client::DoMaxStreams() {
  gpr_log(GPR_DEBUG, "Testing max streams");

  // Make an initial call on the channel to ensure the server's max streams
  // setting is received
  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             grpc::string(kLargeResponseSize, '\0'));

  std::vector<std::thread> test_threads;

  for (int i = 0; i < 10; i++) {
    test_threads.emplace_back(
        std::thread(&Http2Client::MaxStreamsWorker, this, channel_));
  }

  for (auto it = test_threads.begin(); it != test_threads.end(); it++) {
    it->join();
  }

  gpr_log(GPR_DEBUG, "Done testing max streams");
  return true;
}

}  // namespace testing
}  // namespace grpc

DEFINE_int32(server_port, 0, "Server port.");
DEFINE_string(server_host, "localhost", "Server host to connect to");
DEFINE_string(test_case, "rst_after_header",
              "Configure different test cases. Valid options are:\n\n"
              "goaway\n"
              "max_streams\n"
              "ping\n"
              "rst_after_data\n"
              "rst_after_header\n"
              "rst_during_data\n");

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  GPR_ASSERT(FLAGS_server_port);
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", FLAGS_server_host.c_str(),
           FLAGS_server_port);
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateTestChannel(host_port, false);
  GPR_ASSERT(channel->WaitForConnected(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(300, GPR_TIMESPAN))));
  grpc::testing::Http2Client client(channel);
  gpr_log(GPR_INFO, "Testing case: %s", FLAGS_test_case.c_str());
  int ret = 0;
  if (FLAGS_test_case == "rst_after_header") {
    client.DoRstAfterHeader();
  } else if (FLAGS_test_case == "rst_after_data") {
    client.DoRstAfterData();
  } else if (FLAGS_test_case == "rst_during_data") {
    client.DoRstDuringData();
  } else if (FLAGS_test_case == "goaway") {
    client.DoGoaway();
  } else if (FLAGS_test_case == "ping") {
    client.DoPing();
  } else if (FLAGS_test_case == "max_streams") {
    client.DoMaxStreams();
  } else {
    const char* testcases[] = {
        "goaway",         "max_streams",      "ping",
        "rst_after_data", "rst_after_header", "rst_during_data"};
    char* joined_testcases =
        gpr_strjoin_sep(testcases, GPR_ARRAY_SIZE(testcases), "\n", NULL);

    gpr_log(GPR_ERROR, "Unsupported test case %s. Valid options are\n%s",
            FLAGS_test_case.c_str(), joined_testcases);
    gpr_free(joined_testcases);
    ret = 1;
  }

  return ret;
}
