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

#include <memory>
#include <sstream>

#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_config.h"

DEFINE_int32(server_control_port, 0, "Server port for control rpcs.");
DEFINE_int32(server_retry_port, 0, "Server port for testing reconnection.");
DEFINE_string(server_host, "localhost", "Server host to connect to");
DEFINE_int32(max_reconnect_backoff_ms, 0,
             "Maximum backoff time, or 0 for default.");

using grpc::CallCredentials;
using grpc::Channel;
using grpc::ChannelArguments;
using grpc::ClientContext;
using grpc::CreateTestChannel;
using grpc::Status;
using grpc::testing::Empty;
using grpc::testing::ReconnectInfo;
using grpc::testing::ReconnectParams;
using grpc::testing::ReconnectService;

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  GPR_ASSERT(FLAGS_server_control_port);
  GPR_ASSERT(FLAGS_server_retry_port);

  std::ostringstream server_address;
  server_address << FLAGS_server_host << ':' << FLAGS_server_control_port;
  std::unique_ptr<ReconnectService::Stub> control_stub(
      ReconnectService::NewStub(
          CreateTestChannel(server_address.str(), false)));
  ClientContext start_context;
  ReconnectParams reconnect_params;
  reconnect_params.set_max_reconnect_backoff_ms(FLAGS_max_reconnect_backoff_ms);
  Empty empty_response;
  Status start_status =
      control_stub->Start(&start_context, reconnect_params, &empty_response);
  GPR_ASSERT(start_status.ok());

  gpr_log(GPR_INFO, "Starting connections with retries.");
  server_address.str("");
  server_address << FLAGS_server_host << ':' << FLAGS_server_retry_port;
  ChannelArguments channel_args;
  if (FLAGS_max_reconnect_backoff_ms > 0) {
    channel_args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS,
                        FLAGS_max_reconnect_backoff_ms);
  }
  std::shared_ptr<Channel> retry_channel =
      CreateTestChannel(server_address.str(), "foo.test.google.fr", true, false,
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
