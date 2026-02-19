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
#include "src/core/util/grpc_check.h"
#include "src/core/util/host_port.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_oauth2_common.h"
#include "test/core/end2end/fixtures/h2_ssl_cred_reload_fixture.h"
#include "test/core/end2end/fixtures/h2_ssl_tls_common.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/end2end/fixtures/http_common_secure_fixtures.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/test_util/port.h"

namespace grpc_core {

class Ph2InsecureFixture : public InsecureFixture {
 public:
  explicit Ph2InsecureFixture(bool enable_retry) : enable_retry_(enable_retry) {
    // At Least one of the 2 peers MUST be a PH2
    GRPC_DCHECK(IsPromiseBasedHttp2ClientTransportEnabled() ||
                IsPromiseBasedHttp2ServerTransportEnabled());
  }

  ChannelArgs MutateClientArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_ENABLE_CHANNELZ, true)
        .SetIfUnset(GRPC_ARG_ENABLE_RETRIES, enable_retry_);
  }

  ChannelArgs MutateServerArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_ENABLE_CHANNELZ, true);
  }

 private:
  const bool enable_retry_;
};

#define GRPC_HTTP2_PH2_FEATURE_MASK \
  (FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_IS_CALL_V3 | FEATURE_MASK_IS_PH2_CLIENT)

// This macro defines a set of cancellation and deadline tests that are
// frequently broken. Grouping them here allows them to be added to the
// GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST list easily.
#define CANCEL_SUITE                        \
  "|CoreEnd2endTests.CancelAfterAccept"     \
  "|CoreEnd2endTests.CancelAfterClientDone" \
  "|CoreEnd2endTests.CancelAfterInvoke3"    \
  "|CoreEnd2endTests.CancelAfterInvoke4"    \
  "|CoreEnd2endTests.CancelAfterInvoke5"    \
  "|CoreEnd2endTests.CancelAfterInvoke6"    \
  "|CoreEnd2endTests.CancelAfterRoundTrip"  \
  "|CoreEnd2endTests.CancelWithStatus1"     \
  "|CoreEnd2endTests.CancelWithStatus2"     \
  "|CoreEnd2endTests.CancelWithStatus3"     \
  "|CoreEnd2endTests.CancelWithStatus4"

#define DEADLINE_SUITE                      \
  "|CoreDeadlineTests.DeadlineAfterInvoke3" \
  "|CoreDeadlineTests.DeadlineAfterInvoke4" \
  "|CoreDeadlineTests.DeadlineAfterInvoke5" \
  "|CoreDeadlineTests.DeadlineAfterInvoke6" \
  "|CoreDeadlineTests.DeadlineAfterRoundTrip"

#define RETRY_SUITE "|RetryTests|RetryHttp2Tests"
#define SECURE_SUITE                                                   \
  "|SecureEnd2endTests|PerCallCredsTests|PerCallCredsOnInsecureTests|" \
  "ProxyAuthTests"

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_RETRY_AVOID_LIST \
  "|RetryHttp2Tests.Ping"                                    \
  "|RetryHttp2Tests.BadPing"                                 \
  "|RetryHttp2Tests.RetryTransparentMaxConcurrentStreams"    \
  "|RetryHttp2Tests.HighInitialSeqno"                        \
  "|RetryHttp2Tests.CancelDuringDelay"

#define LARGE_METADATA_SUITE                                                   \
  "|Http2SingleHopTests.RequestWithLargeMetadataUnderSoftLimit"                \
  "|Http2SingleHopTests.RequestWithLargeMetadataBetweenSoftAndHardLimits"      \
  "|Http2SingleHopTests.RequestWithLargeMetadataAboveHardLimit"                \
  "|Http2SingleHopTests.RequestWithLargeMetadataSoftLimitAboveHardLimit"       \
  "|Http2SingleHopTests.RequestWithLargeMetadataSoftLimitOverridesDefaultHard" \
  "|Http2SingleHopTests.RequestWithLargeMetadataHardLimitOverridesDefaultSoft" \
  "|Http2SingleHopTests.RequestWithLargeMetadataHardLimitBelowDefaultHard"     \
  "|Http2SingleHopTests.RequestWithLargeMetadataSoftLimitBelowDefaultSoft"

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST       \
  "|Http2SingleHopTests.MaxConcurrentStreams"                \
  "|Http2SingleHopTests.MaxConcurrentStreamsTimeoutOnFirst"  \
  "|Http2SingleHopTests.MaxConcurrentStreamsTimeoutOnSecond" \
  "|Http2SingleHopTests.MaxConcurrentStreamsRejectOnClient"  \
  "|Http2SingleHopTests.ServerMaxConcurrentStreams"          \
  "|Http2Tests.GracefulServerShutdown"                       \
  "|Http2Tests.MaxAgeForciblyClose"                          \
  "|Http2Tests.MaxAgeGracefullyClose"

#define GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE     \
  "|CoreEnd2endTests|CoreDeadlineTests|CoreLargeSendTests|" \
  "CoreClientChannelTests|CoreDeadlineSingleHopTests|"      \
  "Http2SingleHopTests|Http2Tests|CoreDeadlineSingleHopTests"

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  std::vector<CoreTestConfiguration> list_of_configs;
  if (IsExperimentEnabled(
          ExperimentIds::kExperimentIdPromiseBasedHttp2ClientTransport)) {
    std::vector<CoreTestConfiguration> skip_windows_configs;
    // TODO(tjagtap) : [PH2][P3] : Add configs for
    // 1. CHTTP2 Client vs PH2 server
    // 2. and PH2 Client vs PH2 server
    list_of_configs = std::vector<CoreTestConfiguration>{CoreTestConfiguration{
        /*name=*/GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG,
        /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            GRPC_HTTP2_PH2_FEATURE_MASK | FEATURE_MASK_DO_NOT_FUZZ |
            FEATURE_MASK_DOES_NOT_SUPPORT_RETRY,
        // TODO(tjagtap) : [PH2][P3] Explore if fuzzing can be enabled.
        /*overridden_call_host=*/nullptr,
        /*create_fixture=*/
        [](const ChannelArgs& /*client_args*/,
           const ChannelArgs& /*server_args*/) {
          return std::make_unique<Ph2InsecureFixture>(
              /*enable_retry=*/false);
        },
        /* include_test_suites */
        GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE,
        /* include_specific_tests */
        "",
        /* exclude_specific_tests */
        GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST}};

    skip_windows_configs = std::vector<CoreTestConfiguration>{
        CoreTestConfiguration{
            /*name=*/GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_FAKE_SECURITY,
            /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            nullptr,
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<FakesecFixture>();
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_INSECURE_CREDENTIALS,
            /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            nullptr,
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<InsecureCredsFixture>();
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_FULLSTACK_LOCAL_IPV4,
            /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_DO_NOT_FUZZ | FEATURE_MASK_IS_LOCAL_TCP_CREDS |
                GRPC_HTTP2_PH2_FEATURE_MASK,
            nullptr,
            [](const ChannelArgs& /*client_args*/,
               const ChannelArgs& /*server_args*/) {
              int port = grpc_pick_unused_port_or_die();
              return std::make_unique<LocalTestFixture>(
                  JoinHostPort("127.0.0.1", port), LOCAL_TCP);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_FULLSTACK_LOCAL_IPV6,
            /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_DO_NOT_FUZZ | FEATURE_MASK_IS_LOCAL_TCP_CREDS |
                GRPC_HTTP2_PH2_FEATURE_MASK,
            nullptr,
            [](const ChannelArgs& /*client_args*/,
               const ChannelArgs& /*server_args*/) {
              int port = grpc_pick_unused_port_or_die();
              return std::make_unique<LocalTestFixture>(
                  JoinHostPort("[::1]", port), LOCAL_TCP);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SSL_PROXY,
            /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_IS_SECURE |
                FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_DO_NOT_FUZZ | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs& client_args, const ChannelArgs& server_args) {
              return std::make_unique<SslProxyFixture>(client_args,
                                                       server_args);
            },
            // TODO(akshitpatel) : [PH2][P3] : Add all test suites for proxy.
            /* include_test_suites */
            SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_TLS12,
            /*feature_mask=*/FEATURE_MASK_IS_SECURE |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<Oauth2Fixture>(grpc_tls_version::TLS1_2);
            },
            /* include_test_suites */
            SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_WITH_OAUTH2_FULLSTACK_TLS13,
            /*feature_mask=*/FEATURE_MASK_IS_SECURE |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<Oauth2Fixture>(grpc_tls_version::TLS1_3);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_FULLSTACK_TLS12,
            /*feature_mask=*/FEATURE_MASK_IS_SECURE |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<SslTlsFixture>(grpc_tls_version::TLS1_2);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_FULLSTACK_TLS13,
            /*feature_mask=*/FEATURE_MASK_IS_SECURE |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<SslTlsFixture>(grpc_tls_version::TLS1_3);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SSL_CRED_RELOAD_TLS12,
            /*feature_mask=*/FEATURE_MASK_IS_SECURE |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<SslCredReloadFixture>(TLS1_2);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SSL_CRED_RELOAD_TLS13,
            /*feature_mask=*/FEATURE_MASK_IS_SECURE |
                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST |
                FEATURE_MASK_DO_NOT_GTEST | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<SslCredReloadFixture>(TLS1_3);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_CERT_WATCHER_PROVIDER_ASYNC_VERIFIER_TLS13,
            /*feature_mask=*/kH2TLSFeatureMask | FEATURE_MASK_DO_NOT_FUZZ |
                GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<TlsFixture>(
                  SecurityPrimitives::TlsVersion::V_13,
                  SecurityPrimitives::ProviderType::FILE_PROVIDER,
                  SecurityPrimitives::VerifierType::EXTERNAL_ASYNC_VERIFIER);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_CERT_WATCHER_PROVIDER_SYNC_VERIFIER_TLS12,
            /*feature_mask=*/kH2TLSFeatureMask | FEATURE_MASK_DO_NOT_FUZZ |
                GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<TlsFixture>(
                  SecurityPrimitives::TlsVersion::V_12,
                  SecurityPrimitives::ProviderType::FILE_PROVIDER,
                  SecurityPrimitives::VerifierType::HOSTNAME_VERIFIER);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_SIMPLE_SSL_FULLSTACK,
            /*feature_mask=*/kH2TLSFeatureMask | GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<TlsFixture>(
                  SecurityPrimitives::TlsVersion::V_12,
                  SecurityPrimitives::ProviderType::STATIC_PROVIDER,
                  SecurityPrimitives::VerifierType::EXTERNAL_SYNC_VERIFIER);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/
            GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_STATIC_PROVIDER_ASYNC_VERIFIER_TLS13,
            /*feature_mask=*/kH2TLSFeatureMask | FEATURE_MASK_DO_NOT_FUZZ |
                GRPC_HTTP2_PH2_FEATURE_MASK,
            "foo.test.google.fr",
            [](const ChannelArgs&, const ChannelArgs&) {
              return std::make_unique<TlsFixture>(
                  SecurityPrimitives::TlsVersion::V_13,
                  SecurityPrimitives::ProviderType::STATIC_PROVIDER,
                  SecurityPrimitives::VerifierType::EXTERNAL_ASYNC_VERIFIER);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST},
        CoreTestConfiguration{
            /*name=*/GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG_RETRY,
            /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_IS_CALL_V3 |
                FEATURE_MASK_IS_PH2_CLIENT | FEATURE_MASK_DO_NOT_FUZZ,
            // TODO(tjagtap) : [PH2][P3] Explore if fuzzing can be enabled.
            /*overridden_call_host=*/nullptr,
            /*create_fixture=*/
            [](const ChannelArgs& /*client_args*/,
               const ChannelArgs& /*server_args*/) {
              return std::make_unique<Ph2InsecureFixture>(
                  /*enable_retry=*/true);
            },
            /* include_test_suites */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_ALLOW_SUITE RETRY_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_AVOID_LIST
                GRPC_HTTP2_PROMISE_CLIENT_TRANSPORT_RETRY_AVOID_LIST}};

#ifndef GPR_WINDOWS
    // TODO(akshitpatel): [PH2][P5] - Re-enable tests on Windows.
    // Due to capacity constraints, we are skipping a few tests on windows.
    list_of_configs.insert(list_of_configs.end(), skip_windows_configs.begin(),
                           skip_windows_configs.end());
#endif
  }
  return list_of_configs;
}

}  // namespace grpc_core
