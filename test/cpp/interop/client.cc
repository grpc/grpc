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

#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <gflags/gflags.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/cpp/interop/client_helper.h"
#include "test/cpp/interop/interop_client.h"
#include "test/cpp/util/test_config.h"

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
              "cancel_after_begin : cancel stream after starting it; "
              "cancel_after_first_response: cancel on first response; "
              "timeout_on_sleeping_server: deadline exceeds on stream; "
              "service_account_creds : large_unary with service_account auth; "
              "compute_engine_creds: large_unary with compute engine auth; "
              "jwt_token_creds: large_unary with JWT token auth; "
              "oauth2_auth_token: raw oauth2 access token auth; "
              "all : all of above.");
DEFINE_string(default_service_account, "",
              "Email of GCE default service account");
DEFINE_string(service_account_key_file, "",
              "Path to service account json key file.");
DEFINE_string(oauth_scope, "", "Scope for OAuth tokens.");

using grpc::testing::CreateChannelForTestCase;
using grpc::testing::GetServiceAccountJsonKey;

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  int ret = 0;
  grpc::testing::InteropClient client(
      CreateChannelForTestCase(FLAGS_test_case));
  if (FLAGS_test_case == "empty_unary") {
    client.DoEmpty();
  } else if (FLAGS_test_case == "large_unary") {
    client.DoLargeUnary();
  } else if (FLAGS_test_case == "client_streaming") {
    client.DoRequestStreaming();
  } else if (FLAGS_test_case == "server_streaming") {
    client.DoResponseStreaming();
  } else if (FLAGS_test_case == "slow_consumer") {
    client.DoResponseStreamingWithSlowConsumer();
  } else if (FLAGS_test_case == "half_duplex") {
    client.DoHalfDuplex();
  } else if (FLAGS_test_case == "ping_pong") {
    client.DoPingPong();
  } else if (FLAGS_test_case == "cancel_after_begin") {
    client.DoCancelAfterBegin();
  } else if (FLAGS_test_case == "cancel_after_first_response") {
    client.DoCancelAfterFirstResponse();
  } else if (FLAGS_test_case == "timeout_on_sleeping_server") {
    client.DoTimeoutOnSleepingServer();
  } else if (FLAGS_test_case == "service_account_creds") {
    grpc::string json_key = GetServiceAccountJsonKey();
    client.DoServiceAccountCreds(json_key, FLAGS_oauth_scope);
  } else if (FLAGS_test_case == "compute_engine_creds") {
    client.DoComputeEngineCreds(FLAGS_default_service_account,
                                FLAGS_oauth_scope);
  } else if (FLAGS_test_case == "jwt_token_creds") {
    grpc::string json_key = GetServiceAccountJsonKey();
    client.DoJwtTokenCreds(json_key);
  } else if (FLAGS_test_case == "oauth2_auth_token") {
    grpc::string json_key = GetServiceAccountJsonKey();
    client.DoOauth2AuthToken(json_key, FLAGS_oauth_scope);
  } else if (FLAGS_test_case == "all") {
    client.DoEmpty();
    client.DoLargeUnary();
    client.DoRequestStreaming();
    client.DoResponseStreaming();
    client.DoHalfDuplex();
    client.DoPingPong();
    client.DoCancelAfterBegin();
    client.DoCancelAfterFirstResponse();
    client.DoTimeoutOnSleepingServer();
    // service_account_creds and jwt_token_creds can only run with ssl.
    if (FLAGS_enable_ssl) {
      grpc::string json_key = GetServiceAccountJsonKey();
      client.DoServiceAccountCreds(json_key, FLAGS_oauth_scope);
      client.DoJwtTokenCreds(json_key);
      client.DoOauth2AuthToken(json_key, FLAGS_oauth_scope);
    }
    // compute_engine_creds only runs in GCE.
  } else {
    gpr_log(
        GPR_ERROR,
        "Unsupported test case %s. Valid options are all|empty_unary|"
        "large_unary|client_streaming|server_streaming|half_duplex|ping_pong|"
        "cancel_after_begin|cancel_after_first_response|"
        "timeout_on_sleeping_server|service_account_creds|compute_engine_creds|"
        "jwt_token_creds|oauth2_auth_token",
        FLAGS_test_case.c_str());
    ret = 1;
  }

  return ret;
}
