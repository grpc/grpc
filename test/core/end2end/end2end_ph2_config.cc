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
    GRPC_DCHECK(IsPh2Test());
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

///////////////////////////////////////////////////////////////////////////////
// Avoid lists helpers

#define GRPC_HTTP2_PH2_CLIENT_RETRY_AVOID_LIST            \
  "|RetryHttp2Tests.Ping"                                 \
  "|RetryHttp2Tests.BadPing"                              \
  "|RetryHttp2Tests.RetryTransparentMaxConcurrentStreams" \
  "|RetryHttp2Tests.HighInitialSeqno"                     \
  "|RetryHttp2Tests.CancelDuringDelay"                    \
  "|RetryTests.CancelDuringDelay"                         \
  "|CoreEnd2endTests.CancelAfterAccept"

#define GRPC_HTTP2_PH2_CLIENT_ONLY_AVOID_LIST                \
  "|Http2SingleHopTests.MaxConcurrentStreams"                \
  "|Http2SingleHopTests.MaxConcurrentStreamsTimeoutOnFirst"  \
  "|Http2SingleHopTests.MaxConcurrentStreamsTimeoutOnSecond" \
  "|Http2SingleHopTests.MaxConcurrentStreamsRejectOnClient"  \
  "|CoreEnd2endTests.CancelAfterAccept"

// RequestResponseWithPayload10Times and InvokeLargeRequest flake about 6%.
#define GRPC_HTTP2_PH2_SERVER_ONLY_AVOID_LIST             \
  "|CoreLargeSendTests.RequestResponseWithPayload10Times" \
  "|Http2Tests.HighInitialSeqno"                          \
  "|Http2SingleHopTests.InvokeLargeRequest"

#define GRPC_HTTP2_PH2_CLIENT_SERVER_ONLY_AVOID_LIST ""

#define GRPC_HTTP2_PH2_COMMON_AVOID_LIST  \
  "|Http2SingleHopTests.KeepaliveTimeout" \
  "|Http2Tests.GracefulServerShutdown"    \
  "|Http2Tests.MaxAgeForciblyClose"       \
  "|Http2Tests.MaxAgeGracefullyClose"     \
  "|Http2SingleHopTests.ServerMaxConcurrentStreams"

////////////////////////////////////////////////////////////////////////////////
// Allow List of test suites for all configs

#define GRPC_HTTP2_PH2_ALLOW_SUITE                          \
  "|CoreEnd2endTests|CoreDeadlineTests|CoreLargeSendTests|" \
  "CoreClientChannelTests|CoreDeadlineSingleHopTests|"      \
  "Http2Tests|Http2SingleHopTests"

#define RETRY_SUITE "|RetryTests|RetryHttp2Tests"

#define SECURE_SUITE                                                   \
  "|SecureEnd2endTests|PerCallCredsTests|PerCallCredsOnInsecureTests|" \
  "ProxyAuthTests"

///////////////////////////////////////////////////////////////////////////////
// Avoid lists for each of the following configs:
// 1. PH2 client and CHTTP2 server
// 2. CHTTP2 client and PH2 server
// 3. PH2 client and PH2 server

#define GRPC_HTTP2_PH2_CLIENT_AVOID_LIST \
  GRPC_HTTP2_PH2_CLIENT_ONLY_AVOID_LIST  \
  GRPC_HTTP2_PH2_COMMON_AVOID_LIST

#define GRPC_HTTP2_PH2_SERVER_AVOID_LIST \
  GRPC_HTTP2_PH2_SERVER_ONLY_AVOID_LIST  \
  GRPC_HTTP2_PH2_COMMON_AVOID_LIST

#define GRPC_HTTP2_PH2_CLIENT_SERVER_AVOID_LIST \
  GRPC_HTTP2_PH2_CLIENT_ONLY_AVOID_LIST         \
  GRPC_HTTP2_PH2_SERVER_ONLY_AVOID_LIST         \
  GRPC_HTTP2_PH2_COMMON_AVOID_LIST              \
  GRPC_HTTP2_PH2_CLIENT_SERVER_ONLY_AVOID_LIST

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  std::vector<CoreTestConfiguration> list_of_configs;
  if (IsExperimentEnabled(ExperimentIds::kExperimentIdPh2Client)) {
    std::vector<CoreTestConfiguration> skip_windows_configs;
    std::vector<CoreTestConfiguration> ph2client_chttp2server_configs;
    // TODO(akshitpatel) : [PH2][P4] : This test config has been enabled for
    // fuzzing. If lot of failures occur, then disable fuzzing for this config.
    ph2client_chttp2server_configs =
        std::vector<CoreTestConfiguration>{CoreTestConfiguration{
            /*name=*/GRPC_HTTP2_PH2_CLIENT_CHTTP2_SERVER_CONFIG,
            /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                GRPC_HTTP2_PH2_FEATURE_MASK |
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
            GRPC_HTTP2_PH2_ALLOW_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST}};

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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE SECURE_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST},
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
            GRPC_HTTP2_PH2_ALLOW_SUITE RETRY_SUITE,
            /* include_specific_tests */
            "",
            /* exclude_specific_tests */
            GRPC_HTTP2_PH2_CLIENT_AVOID_LIST
                GRPC_HTTP2_PH2_CLIENT_RETRY_AVOID_LIST}};

#ifndef GPR_WINDOWS
    // TODO(akshitpatel): [PH2][P5] - Re-enable tests on Windows.
    // Due to capacity constraints, we are skipping a few tests on windows.
    ph2client_chttp2server_configs.insert(ph2client_chttp2server_configs.end(),
                                          skip_windows_configs.begin(),
                                          skip_windows_configs.end());
#endif
    list_of_configs.insert(list_of_configs.end(),
                           ph2client_chttp2server_configs.begin(),
                           ph2client_chttp2server_configs.end());
  }

  // Config for chttp2 client and ph2 server.
  if (IsPh2ServerEnabled()) {
    std::vector<CoreTestConfiguration> chttp2client_ph2server_configs;

    // TODO(akshitpatel) : [PH2][P4] : This test config has been enabled for
    // fuzzing. If lot of failures occur, then disable fuzzing for this config.
    chttp2client_ph2server_configs.push_back(CoreTestConfiguration{
        /*name=*/GRPC_HTTP2_CHTTP2_CLIENT_PH2_SERVER_CONFIG,
        /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            GRPC_HTTP2_PH2_FEATURE_MASK | FEATURE_MASK_DOES_NOT_SUPPORT_RETRY,
        /*overridden_call_host=*/nullptr,
        /*create_fixture=*/
        [](const ChannelArgs& /*client_args*/,
           const ChannelArgs& /*server_args*/) {
          return std::make_unique<Ph2InsecureFixture>(
              /*enable_retry=*/false);
        },
        /* include_test_suites */
        GRPC_HTTP2_PH2_ALLOW_SUITE,
        /* include_specific_tests */
        "",
        /* exclude_specific_tests */
        GRPC_HTTP2_PH2_SERVER_AVOID_LIST});
    list_of_configs.insert(list_of_configs.end(),
                           chttp2client_ph2server_configs.begin(),
                           chttp2client_ph2server_configs.end());
  }

  // Config for ph2 client and ph2 server.
  if (IsPh2ClientServerEnabled()) {
    std::vector<CoreTestConfiguration> ph2client_ph2server_configs;

    // TODO(akshitpatel) : [PH2][P4] : This test config has been enabled for
    // fuzzing. If lot of failures occur, then disable fuzzing for this config.
    ph2client_ph2server_configs.push_back(CoreTestConfiguration{
        /*name=*/GRPC_HTTP2_PH2_CLIENT_PH2_SERVER_CONFIG,
        /*feature_mask=*/FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            GRPC_HTTP2_PH2_FEATURE_MASK | FEATURE_MASK_DOES_NOT_SUPPORT_RETRY,
        /*overridden_call_host=*/nullptr,
        /*create_fixture=*/
        [](const ChannelArgs& /*client_args*/,
           const ChannelArgs& /*server_args*/) {
          return std::make_unique<Ph2InsecureFixture>(
              /*enable_retry=*/false);
        },
        /* include_test_suites */
        GRPC_HTTP2_PH2_ALLOW_SUITE,
        /* include_specific_tests */
        "",
        /* exclude_specific_tests */
        GRPC_HTTP2_PH2_CLIENT_SERVER_AVOID_LIST});
    list_of_configs.insert(list_of_configs.end(),
                           ph2client_ph2server_configs.begin(),
                           ph2client_ph2server_configs.end());
  }

  return list_of_configs;
}

}  // namespace grpc_core
