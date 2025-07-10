// Copyright 2025 gRPC authors.
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

#include <grpc/compression.h>
#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/time.h>

#include <memory>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/experiments/experiments.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"

namespace grpc_core {

#define GRPC_HTTP2_CORE_CLIENT_CHANNEL_TESTS                    \
  "CoreClientChannelTests.DeadlineAfterAcceptWithServiceConfig" \
  "CoreClientChannelTests.DeadlineAfterRoundTripWithServiceConfig"

#define GRPC_HTTP2_CORE_DEADLINE_SINGLE_HOP_TESTS \
  "CoreDeadlineSingleHopTests."                   \
  "TimeoutBeforeRequestCallWithRegisteredMethodWithPayload"

#define GRPC_HTTP2_CORE_DEADLINE_TESTS

#define GRPC_HTTP2_CORE_END2END_TEST_LIST                                  \
  "|CoreEnd2endTests.BinaryMetadataServerHttp2FallbackClientHttp2Fallback" \
  "|CoreEnd2endTests.BinaryMetadataServerHttp2FallbackClientTrueBinary"    \
  "|CoreEnd2endTests.BinaryMetadataServerTrueBinaryClientHttp2Fallback"    \
  "|CoreEnd2endTests.BinaryMetadataServerTrueBinaryClientTrueBinary"       \
  "|CoreEnd2endTests.CancelWithStatus3"                                    \
  "|CoreEnd2endTests.MaxMessageLengthOnClientOnResponseViaChannelArg"      \
  "|CoreEnd2endTests."                                                     \
  "MaxMessageLengthOnClientOnResponseViaServiceConfigWithIntegerJsonValue" \
  "|CoreEnd2endTests."                                                     \
  "MaxMessageLengthOnClientOnResponseViaServiceConfigWithStringJsonValue"  \
  "|CoreEnd2endTests.PingPongStreaming1"                                   \
  "|CoreEnd2endTests.PingPongStreaming10"                                  \
  "|CoreEnd2endTests.PingPongStreaming10"                                  \
  "|CoreEnd2endTests.PingPongStreaming3"                                   \
  "|CoreEnd2endTests.PingPongStreaming3"                                   \
  "|CoreEnd2endTests.PingPongStreaming30"                                  \
  "|CoreEnd2endTests.SimpleMetadata"                                       \
  "|CoreEnd2endTests.StreamingErrorResponse"                               \
  "|CoreEnd2endTests.StreamingErrorResponse"                               \
  "|CoreEnd2endTests.StreamingErrorResponseRequestStatusEarly"             \
  "|CoreEnd2endTests.StreamingErrorResponseRequestStatusEarly"             \
  "|CoreEnd2endTests."                                                     \
  "StreamingErrorResponseRequestStatusEarlyAndRecvMessageSeparately"       \
  "|CoreEnd2endTests.TrailingMetadata"

#define GRPC_HTTP2_CORE_LARGE_SEND_TESTS           \
  "|CoreLargeSendTests.RequestResponseWithPayload" \
  "|CoreLargeSendTests.RequestResponseWithPayload10Times"

#define GRPC_HTTP2_HTTP2_FULLSTACK_SINGLE_HOP_TESTS

#define GRPC_HTTP2_HTTP2_SINGLE_HOP_TESTS

#define GRPC_HTTP2_HTTP2_TESTS

#define GRPC_HTTP2_NO_LOGGING_TESTS

#define GRPC_HTTP2_PER_CALL_CREDS_ON_INSECURE_TESTS

#define GRPC_HTTP2_PER_CALL_CREDS_TESTS

#define GRPC_HTTP2_PROXY_AUTH_TESTS

#define GRPC_HTTP2_RESOURCE_QUOTA_TESTS

#define GRPC_HTTP2_RETRY_HTTP2_TESTS

#define GRPC_HTTP2_RETRY_TESTS

#define GRPC_HTTP2_SECURE_END2END_TESTS

#define GRPC_HTTP2_WRITE_BUFFERING_TESTS

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST \
  GRPC_HTTP2_CORE_CLIENT_CHANNEL_TESTS                 \
  GRPC_HTTP2_CORE_DEADLINE_SINGLE_HOP_TESTS            \
  GRPC_HTTP2_CORE_DEADLINE_TESTS                       \
  GRPC_HTTP2_CORE_END2END_TEST_LIST                    \
  GRPC_HTTP2_CORE_LARGE_SEND_TESTS                     \
  GRPC_HTTP2_HTTP2_FULLSTACK_SINGLE_HOP_TESTS          \
  GRPC_HTTP2_HTTP2_SINGLE_HOP_TESTS                    \
  GRPC_HTTP2_HTTP2_TESTS                               \
  GRPC_HTTP2_NO_LOGGING_TESTS                          \
  GRPC_HTTP2_PER_CALL_CREDS_ON_INSECURE_TESTS          \
  GRPC_HTTP2_PER_CALL_CREDS_TESTS                      \
  GRPC_HTTP2_PROXY_AUTH_TESTS                          \
  GRPC_HTTP2_RESOURCE_QUOTA_TESTS                      \
  GRPC_HTTP2_RETRY_HTTP2_TESTS                         \
  GRPC_HTTP2_RETRY_TESTS                               \
  GRPC_HTTP2_SECURE_END2END_TESTS                      \
  GRPC_HTTP2_WRITE_BUFFERING_TESTS

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE    \
  "CoreEnd2endTests|CoreDeadlineTests|CoreLargeSendTests|" \
  "CoreClientChannelTests|CoreDeadlineSingleHopTests|"

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  std::vector<CoreTestConfiguration> list_of_configs;
  if (IsExperimentEnabled(
          ExperimentIds::kExperimentIdPromiseBasedHttp2ClientTransport)) {
    list_of_configs.push_back(CoreTestConfiguration{
        /*name=*/GRPC_HTTP2_PH2_CLIENT_TEST_SUITE,
        /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_IS_CALL_V3 |
            FEATURE_MASK_IS_PH2_CLIENT | FEATURE_MASK_DO_NOT_FUZZ,
        // TODO(tjagtap) : [PH2][P3] Explore if fuzzing can be enabled.
        /*overridden_call_host=*/nullptr,
        /*create_fixture=*/
        [](const ChannelArgs& /*client_args*/,
           const ChannelArgs& /*server_args*/) {
          return std::make_unique<InsecureFixture>();
        },
        /* include_test_suites */
        GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE,
        /* include_specific_tests */
        "",
        /* exclude_specific_tests */
        GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST});
  }
  return list_of_configs;
}

}  // namespace grpc_core
