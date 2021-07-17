/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "test/cpp/interop/http2_client.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include <thread>

#include "absl/flags/flag.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

namespace {
const int kLargeRequestSize = 271828;
const int kLargeResponseSize = 314159;
}  // namespace

Http2Client::ServiceStub::ServiceStub(const std::shared_ptr<Channel>& channel)
    : channel_(channel) {
  stub_ = TestService::NewStub(channel);
}

TestService::Stub* Http2Client::ServiceStub::Get() { return stub_.get(); }

Http2Client::Http2Client(const std::shared_ptr<Channel>& channel)
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
  std::string payload(kLargeRequestSize, '\0');
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
  // There is no guarantee that data would be received.

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
             std::string(kLargeResponseSize, '\0'));

  // Sleep for one second to give time for client to receive goaway frame.
  gpr_timespec sleep_time = gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(1, GPR_TIMESPAN));
  gpr_sleep_until(sleep_time);

  response.Clear();
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             std::string(kLargeResponseSize, '\0'));
  gpr_log(GPR_DEBUG, "Done testing goaway");
  return true;
}

bool Http2Client::DoPing() {
  gpr_log(GPR_DEBUG, "Sending RPC and expecting ping");
  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             std::string(kLargeResponseSize, '\0'));
  gpr_log(GPR_DEBUG, "Done testing ping");
  return true;
}

void Http2Client::MaxStreamsWorker(
    const std::shared_ptr<grpc::Channel>& /*channel*/) {
  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             std::string(kLargeResponseSize, '\0'));
}

bool Http2Client::DoMaxStreams() {
  gpr_log(GPR_DEBUG, "Testing max streams");

  // Make an initial call on the channel to ensure the server's max streams
  // setting is received
  SimpleResponse response;
  AssertStatusCode(SendUnaryCall(&response), grpc::StatusCode::OK);
  GPR_ASSERT(response.payload().body() ==
             std::string(kLargeResponseSize, '\0'));

  std::vector<std::thread> test_threads;
  test_threads.reserve(10);
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

ABSL_FLAG(int32_t, server_port, 0, "Server port.");
ABSL_FLAG(std::string, server_host, "localhost", "Server host to connect to");
ABSL_FLAG(std::string, test_case, "rst_after_header",
          "Configure different test cases. Valid options are:\n\n"
          "goaway\n"
          "max_streams\n"
          "ping\n"
          "rst_after_data\n"
          "rst_after_header\n"
          "rst_during_data\n");

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  GPR_ASSERT(absl::GetFlag(FLAGS_server_port));
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d",
           absl::GetFlag(FLAGS_server_host).c_str(),
           absl::GetFlag(FLAGS_server_port));
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateTestChannel(host_port, grpc::testing::INSECURE);
  GPR_ASSERT(channel->WaitForConnected(gpr_time_add(
      gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(300, GPR_TIMESPAN))));
  grpc::testing::Http2Client client(channel);
  gpr_log(GPR_INFO, "Testing case: %s", absl::GetFlag(FLAGS_test_case).c_str());
  int ret = 0;
  if (absl::GetFlag(FLAGS_test_case) == "rst_after_header") {
    client.DoRstAfterHeader();
  } else if (absl::GetFlag(FLAGS_test_case) == "rst_after_data") {
    client.DoRstAfterData();
  } else if (absl::GetFlag(FLAGS_test_case) == "rst_during_data") {
    client.DoRstDuringData();
  } else if (absl::GetFlag(FLAGS_test_case) == "goaway") {
    client.DoGoaway();
  } else if (absl::GetFlag(FLAGS_test_case) == "ping") {
    client.DoPing();
  } else if (absl::GetFlag(FLAGS_test_case) == "max_streams") {
    client.DoMaxStreams();
  } else {
    const char* testcases[] = {
        "goaway",         "max_streams",      "ping",
        "rst_after_data", "rst_after_header", "rst_during_data"};
    char* joined_testcases =
        gpr_strjoin_sep(testcases, GPR_ARRAY_SIZE(testcases), "\n", nullptr);

    gpr_log(GPR_ERROR, "Unsupported test case %s. Valid options are\n%s",
            absl::GetFlag(FLAGS_test_case).c_str(), joined_testcases);
    gpr_free(joined_testcases);
    ret = 1;
  }

  return ret;
}
