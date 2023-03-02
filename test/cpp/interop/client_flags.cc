//
//
// Copyright 2022 gRPC authors.
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

#include "test/cpp/interop/client_flags.h"

#include "absl/flags/flag.h"

ABSL_FLAG(bool, use_alts, false,
          "Whether to use alts. Enable alts will disable tls.");
ABSL_FLAG(bool, use_tls, false, "Whether to use tls.");
ABSL_FLAG(std::string, custom_credentials_type, "",
          "User provided credentials type.");
ABSL_FLAG(bool, use_test_ca, false, "False to use SSL roots for google");
ABSL_FLAG(int32_t, server_port, 0, "Server port.");
ABSL_FLAG(std::string, server_host, "localhost", "Server host to connect to");
ABSL_FLAG(std::string, server_host_override, "",
          "Override the server host which is sent in HTTP header");
ABSL_FLAG(
    std::string, test_case, "large_unary",
    "Configure different test cases. Valid options are:\n\n"
    "all : all test cases;\n"

    // TODO(veblush): Replace the help message with the following full message
    // once Abseil fixes the flag-help compiler error on Windows. (b/171659833)
    //
    //"cancel_after_begin : cancel stream after starting it;\n"
    //"cancel_after_first_response: cancel on first response;\n"
    //"channel_soak: sends 'soak_iterations' rpcs, rebuilds channel each
    // time;\n" "client_compressed_streaming : compressed request streaming with
    //" "client_compressed_unary : single compressed request;\n"
    //"client_streaming : request streaming with single response;\n"
    //"compute_engine_creds: large_unary with compute engine auth;\n"
    //"custom_metadata: server will echo custom metadata;\n"
    //"empty_stream : bi-di stream with no request/response;\n"
    //"empty_unary : empty (zero bytes) request and response;\n"
    //"google_default_credentials: large unary using GDC;\n"
    //"half_duplex : half-duplex streaming;\n"
    //"jwt_token_creds: large_unary with JWT token auth;\n"
    //"large_unary : single request and (large) response;\n"
    //"long_lived_channel: sends large_unary rpcs over a long-lived channel;\n"
    //"oauth2_auth_token: raw oauth2 access token auth;\n"
    //"per_rpc_creds: raw oauth2 access token on a single rpc;\n"
    //"ping_pong : full-duplex streaming;\n"
    //"response streaming;\n"
    //"rpc_soak: 'sends soak_iterations' large_unary rpcs;\n"
    //"server_compressed_streaming : single request with compressed "
    //"server_compressed_unary : single compressed response;\n"
    //"server_streaming : single request with response streaming;\n"
    //"slow_consumer : single request with response streaming with "
    //"slow client consumer;\n"
    //"special_status_message: verify Unicode and whitespace in status
    // message;\n" "status_code_and_message: verify status code & message;\n"
    //"timeout_on_sleeping_server: deadline exceeds on stream;\n"
    //"unimplemented_method: client calls an unimplemented method;\n"
    //"unimplemented_service: client calls an unimplemented service;\n"
    //
);
ABSL_FLAG(int32_t, num_times, 1, "Number of times to run the test case");
ABSL_FLAG(std::string, default_service_account, "",
          "Email of GCE default service account");
ABSL_FLAG(std::string, service_account_key_file, "",
          "Path to service account json key file.");
ABSL_FLAG(std::string, oauth_scope, "", "Scope for OAuth tokens.");
ABSL_FLAG(bool, do_not_abort_on_transient_failures, false,
          "If set to 'true', abort() is not called in case of transient "
          "failures (i.e failures that are temporary and will likely go away "
          "on retrying; like a temporary connection failure) and an error "
          "message is printed instead. Note that this flag just controls "
          "whether abort() is called or not. It does not control whether the "
          "test is retried in case of transient failures (and currently the "
          "interop tests are not retried even if this flag is set to true)");
ABSL_FLAG(int32_t, soak_iterations, 1000,
          "The number of iterations to use for the two soak tests; rpc_soak "
          "and channel_soak.");
ABSL_FLAG(int32_t, soak_max_failures, 0,
          "The number of iterations in soak tests that are allowed to fail "
          "(either due to non-OK status code or exceeding the "
          "per-iteration max acceptable latency).");
ABSL_FLAG(int32_t, soak_per_iteration_max_acceptable_latency_ms, 0,
          "The number of milliseconds a single iteration in the two soak "
          "tests (rpc_soak and channel_soak) should take.");
ABSL_FLAG(int32_t, soak_overall_timeout_seconds, 0,
          "The overall number of seconds after which a soak test should "
          "stop and fail, if the desired number of iterations have not yet "
          "completed.");
ABSL_FLAG(int32_t, soak_min_time_ms_between_rpcs, 0,
          "The minimum time in milliseconds between consecutive RPCs in a "
          "soak test (rpc_soak or channel_soak), useful for limiting QPS");
ABSL_FLAG(int32_t, iteration_interval, 10,
          "The interval in seconds between rpcs. This is used by "
          "long_connection test");
ABSL_FLAG(std::string, additional_metadata, "",
          "Additional metadata to send in each request, as a "
          "semicolon-separated list of key:value pairs.");
ABSL_FLAG(
    bool, log_metadata_and_status, false,
    "If set to 'true', will print received initial and trailing metadata, "
    "grpc-status and error message to the console, in a stable format.");
