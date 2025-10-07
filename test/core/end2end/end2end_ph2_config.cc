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

class Ph2InsecureFixture : public InsecureFixture {
 public:
  Ph2InsecureFixture() {
    // At Least one of the 2 peers MUST be a PH2
    GRPC_DCHECK(IsPromiseBasedHttp2ClientTransportEnabled() ||
                IsPromiseBasedHttp2ServerTransportEnabled());
  }

  ChannelArgs MutateClientArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_ENABLE_CHANNELZ, true);
  }

  ChannelArgs MutateServerArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_ENABLE_CHANNELZ, true);
  }
};

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST                         \
  "CoreClientChannelTests.DeadlineAfterAcceptWithServiceConfig"                \
  "|CoreClientChannelTests.DeadlineAfterRoundTripWithServiceConfig"            \
  "|CoreDeadlineTests.DeadlineAfterRoundTrip"                                  \
  "|CoreDeadlineSingleHopTests."                                               \
  "TimeoutBeforeRequestCallWithRegisteredMethodWithPayload"                    \
  "|CoreEnd2endTests.BinaryMetadataServerHttp2FallbackClientHttp2Fallback"     \
  "|CoreEnd2endTests.BinaryMetadataServerHttp2FallbackClientTrueBinary"        \
  "|CoreEnd2endTests.BinaryMetadataServerTrueBinaryClientTrueBinary"           \
  "|CoreEnd2endTests.TrailingMetadata"                                         \
  "|CoreEnd2endTests.BinaryMetadataServerTrueBinaryClientHttp2Fallback"        \
  "|CoreEnd2endTests.CancelAfterAccept"                                        \
  "|CoreEnd2endTests.CancelAfterClientDone"                                    \
  "|CoreEnd2endTests.CancelAfterInvoke3"                                       \
  "|CoreEnd2endTests.CancelAfterInvoke4"                                       \
  "|CoreEnd2endTests.CancelAfterInvoke5"                                       \
  "|CoreEnd2endTests.CancelAfterInvoke6"                                       \
  "|CoreEnd2endTests.CancelAfterRoundTrip"                                     \
  "|CoreEnd2endTests.CancelWithStatus1"                                        \
  "|CoreEnd2endTests.CancelWithStatus2"                                        \
  "|CoreEnd2endTests.CancelWithStatus3"                                        \
  "|CoreEnd2endTests.CancelWithStatus4"                                        \
  "|CoreEnd2endTests.DeadlineAfterInvoke3"                                     \
  "|CoreEnd2endTests.DeadlineAfterInvoke4"                                     \
  "|CoreEnd2endTests.DeadlineAfterInvoke5"                                     \
  "|CoreEnd2endTests.DeadlineAfterInvoke6"                                     \
  "|CoreEnd2endTests.MaxMessageLengthOnClientOnResponseViaChannelArg"          \
  "|CoreEnd2endTests."                                                         \
  "MaxMessageLengthOnClientOnResponseViaServiceConfigWithIntegerJsonValue"     \
  "|CoreEnd2endTests."                                                         \
  "MaxMessageLengthOnClientOnResponseViaServiceConfigWithStringJsonValue"      \
  "|CoreLargeSendTests.RequestResponseWithPayload"                             \
  "|CoreLargeSendTests.RequestResponseWithPayload10Times"                      \
  "|Http2SingleHopTests.DisabledAlgorithmDecompressInCore"                     \
  "|Http2SingleHopTests.DisabledAlgorithmDecompressInApp"                      \
  "|Http2SingleHopTests."                                                      \
  "RequestWithExceptionallyUncompressedPayloadDecompressInCore"                \
  "|Http2SingleHopTests."                                                      \
  "RequestWithExceptionallyUncompressedPayloadDecompressInApp"                 \
  "|Http2SingleHopTests.RequestWithUncompressedPayloadDecompressInCore"        \
  "|Http2SingleHopTests.RequestWithUncompressedPayloadDecompressInApp"         \
  "|Http2SingleHopTests.RequestWithCompressedPayloadDecompressInCore"          \
  "|Http2SingleHopTests.RequestWithCompressedPayloadDecompressInApp"           \
  "|Http2SingleHopTests."                                                      \
  "RequestWithSendMessageBeforeInitialMetadataDecompressInCore"                \
  "|Http2SingleHopTests."                                                      \
  "RequestWithSendMessageBeforeInitialMetadataDecompressInApp"                 \
  "|Http2SingleHopTests.RequestWithServerLevelDecompressInCore"                \
  "|Http2SingleHopTests.RequestWithServerLevelDecompressInApp"                 \
  "|Http2SingleHopTests."                                                      \
  "RequestWithCompressedPayloadMetadataOverrideNoneToGzipDecompressInCore"     \
  "|Http2SingleHopTests."                                                      \
  "RequestWithCompressedPayloadMetadataOverrideNoneToGzipDecompressInApp"      \
  "|Http2SingleHopTests."                                                      \
  "RequestWithCompressedPayloadMetadataOverrideDeflateToGzipDecompressInCore"  \
  "|Http2SingleHopTests."                                                      \
  "RequestWithCompressedPayloadMetadataOverrideDeflateToGzipDecompressInApp"   \
  "|Http2SingleHopTests."                                                      \
  "RequestWithCompressedPayloadMetadataOverrideDeflateToIdentityDecompressInC" \
  "ore"                                                                        \
  "|Http2SingleHopTests."                                                      \
  "RequestWithCompressedPayloadMetadataOverrideDeflateToIdentityDecompressInA" \
  "pp"                                                                         \
  "|Http2SingleHopTests.RequestWithDefaultHighLevelDecompressInCore"           \
  "|Http2SingleHopTests.RequestWithDefaultMediumLevelDecompressInCore"         \
  "|Http2SingleHopTests.RequestWithDefaultLowLevelDecompressInCore"            \
  "|Http2SingleHopTests.RequestWithDefaultNoneLevelDecompressInCore"           \
  "|Http2SingleHopTests.InvokeLargeRequest"                                    \
  "|Http2SingleHopTests.KeepaliveTimeout"                                      \
  "|Http2SingleHopTests.ReadDelaysKeepalive"                                   \
  "|Http2SingleHopTests.RequestWithLargeMetadataUnderSoftLimit"                \
  "|Http2SingleHopTests.RequestWithLargeMetadataBetweenSoftAndHardLimits"      \
  "|Http2SingleHopTests.RequestWithLargeMetadataAboveHardLimit"                \
  "|Http2SingleHopTests.RequestWithLargeMetadataSoftLimitAboveHardLimit"       \
  "|Http2SingleHopTests.RequestWithLargeMetadataSoftLimitOverridesDefaultHard" \
  "|Http2SingleHopTests.RequestWithLargeMetadataHardLimitOverridesDefaultSoft" \
  "|Http2SingleHopTests.RequestWithLargeMetadataHardLimitBelowDefaultHard"     \
  "|Http2SingleHopTests.RequestWithLargeMetadataSoftLimitBelowDefaultSoft"     \
  "|Http2SingleHopTests.MaxConcurrentStreams"                                  \
  "|Http2SingleHopTests.MaxConcurrentStreamsTimeoutOnFirst"                    \
  "|Http2SingleHopTests.MaxConcurrentStreamsTimeoutOnSecond"                   \
  "|Http2SingleHopTests.MaxConcurrentStreamsRejectOnClient"                    \
  "|Http2SingleHopTests.SimpleDelayedRequestShort"

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE               \
  "CoreEnd2endTests|CoreDeadlineTests|CoreLargeSendTests|"            \
  "CoreClientChannelTests|CoreDeadlineSingleHopTests|NoLoggingTests|" \
  "Http2SingleHopTests"

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  std::vector<CoreTestConfiguration> list_of_configs;
  if (IsExperimentEnabled(
          ExperimentIds::kExperimentIdPromiseBasedHttp2ClientTransport)) {
    // TODO(tjagtap) : [PH2][P3] : Add configs for
    // 1. CHTTP2 Client vs PH2 server
    // 2. and PH2 Client vs PH2 server
    list_of_configs.push_back(CoreTestConfiguration{
        /*name=*/GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG,
        /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_IS_CALL_V3 |
            FEATURE_MASK_IS_PH2_CLIENT | FEATURE_MASK_DO_NOT_FUZZ,
        // TODO(tjagtap) : [PH2][P3] Explore if fuzzing can be enabled.
        /*overridden_call_host=*/nullptr,
        /*create_fixture=*/
        [](const ChannelArgs& /*client_args*/,
           const ChannelArgs& /*server_args*/) {
          return std::make_unique<Ph2InsecureFixture>();
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
