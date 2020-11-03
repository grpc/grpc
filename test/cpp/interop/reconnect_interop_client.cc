/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/support/channel_arguments.h>

#include <memory>
#include <sstream>

#include "absl/flags/flag.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(int32_t, server_control_port, 0, "Server port for control rpcs.");
ABSL_FLAG(int32_t, server_retry_port, 0,
          "Server port for testing reconnection.");
ABSL_FLAG(std::string, server_host, "localhost", "Server host to connect to");
// TODO(Capstan): Consider using absl::Duration
ABSL_FLAG(int32_t, max_reconnect_backoff_ms, 0,
          "Maximum backoff time, or 0 for default.");

using grpc::CallCredentials;
using grpc::Channel;
using grpc::ChannelArguments;
using grpc::ClientContext;
using grpc::CreateTestChannel;
using grpc::Status;
using grpc::testing::Empty;
using grpc::testing::INSECURE;
using grpc::testing::ReconnectInfo;
using grpc::testing::ReconnectParams;
using grpc::testing::ReconnectService;
using grpc::testing::TLS;

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  GPR_ASSERT(absl::GetFlag(FLAGS_server_control_port));
  GPR_ASSERT(absl::GetFlag(FLAGS_server_retry_port));

  std::ostringstream server_address;
  server_address << absl::GetFlag(FLAGS_server_host) << ':'
                 << absl::GetFlag(FLAGS_server_control_port);
  std::unique_ptr<ReconnectService::Stub> control_stub(
      ReconnectService::NewStub(
          CreateTestChannel(server_address.str(), INSECURE)));
  ClientContext start_context;
  ReconnectParams reconnect_params;
  reconnect_params.set_max_reconnect_backoff_ms(
      absl::GetFlag(FLAGS_max_reconnect_backoff_ms));
  Empty empty_response;
  Status start_status =
      control_stub->Start(&start_context, reconnect_params, &empty_response);
  GPR_ASSERT(start_status.ok());

  gpr_log(GPR_INFO, "Starting connections with retries.");
  server_address.str("");
  server_address << absl::GetFlag(FLAGS_server_host) << ':'
                 << absl::GetFlag(FLAGS_server_retry_port);
  ChannelArguments channel_args;
  if (absl::GetFlag(FLAGS_max_reconnect_backoff_ms) > 0) {
    channel_args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS,
                        absl::GetFlag(FLAGS_max_reconnect_backoff_ms));
  }
  std::shared_ptr<Channel> retry_channel =
      CreateTestChannel(server_address.str(), "foo.test.google.fr", TLS, false,
                        std::shared_ptr<CallCredentials>(), channel_args);

  // About 13 retries.
  const int kDeadlineSeconds = 540;
  // Use any rpc to test retry.
  std::unique_ptr<ReconnectService::Stub> retry_stub(
      ReconnectService::NewStub(retry_channel));
  ClientContext retry_context;
  retry_context.set_deadline(std::chrono::system_clock::now() +
                             std::chrono::seconds(kDeadlineSeconds));
  Status retry_status =
      retry_stub->Start(&retry_context, reconnect_params, &empty_response);
  GPR_ASSERT(retry_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED);
  gpr_log(GPR_INFO, "Done retrying, getting final data from server");

  ClientContext stop_context;
  ReconnectInfo response;
  Status stop_status = control_stub->Stop(&stop_context, Empty(), &response);
  GPR_ASSERT(stop_status.ok());
  GPR_ASSERT(response.passed() == true);
  gpr_log(GPR_INFO, "Passed");
  return 0;
}
