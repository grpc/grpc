// Copyright 2023 gRPC authors.
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

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/host_port.h"
#include "src/core/util/sync.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_oauth2_common.h"
#include "test/core/end2end/fixtures/h2_ssl_cred_reload_fixture.h"
#include "test/core/end2end/fixtures/h2_ssl_tls_common.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/end2end/fixtures/http_common_secure_fixtures.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/test_util/port.h"
#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"

// IWYU pragma: no_include <unistd.h>

#ifdef GRPC_POSIX_SOCKET
#include <fcntl.h>

#endif

#ifdef GRPC_POSIX_WAKEUP_FD
#endif

namespace grpc_core {

// Returns the temp directory to create uds in this test.
std::string GetTempDir() {
#ifdef GPR_WINDOWS
  // Windows temp dir usually exceeds uds max path length,
  // so we create a short dir for this test.
  // TODO: find a better solution.
  std::string temp_dir = "C:/tmp/";
  if (CreateDirectoryA(temp_dir.c_str(), NULL) == 0 &&
      ERROR_ALREADY_EXISTS != GetLastError()) {
    Crash(absl::StrCat("Could not create temp dir: ", temp_dir));
  }
  return temp_dir;
#else
  return "/tmp/";
#endif  // GPR_WINDOWS
}

const std::string temp_dir = GetTempDir();

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  return std::vector<CoreTestConfiguration>{
      CoreTestConfiguration{
          "Chttp2FakeSecurityFullstack",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE |
              FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_GTEST,
          nullptr,
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<FakesecFixture>();
          }},
      CoreTestConfiguration{
          "Chttp2InsecureCredentials",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE |
              FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS |
              FEATURE_MASK_DO_NOT_GTEST,
          nullptr,
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<InsecureCredsFixture>();
          },
      },
      CoreTestConfiguration{"Chttp2FullstackLocalIpv4",
                            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                                FEATURE_MASK_IS_HTTP2 |
                                FEATURE_MASK_DO_NOT_FUZZ |
                                FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS |
                                FEATURE_MASK_IS_LOCAL_TCP_CREDS,
                            nullptr,
                            [](const ChannelArgs& /*client_args*/,
                               const ChannelArgs& /*server_args*/) {
                              int port = grpc_pick_unused_port_or_die();
                              return std::make_unique<LocalTestFixture>(
                                  JoinHostPort("127.0.0.1", port), LOCAL_TCP);
                            }},
      CoreTestConfiguration{"Chttp2FullstackLocalIpv6",
                            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
                                FEATURE_MASK_IS_HTTP2 |
                                FEATURE_MASK_DO_NOT_FUZZ |
                                FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS |
                                FEATURE_MASK_IS_LOCAL_TCP_CREDS,
                            nullptr,
                            [](const ChannelArgs& /*client_args*/,
                               const ChannelArgs& /*server_args*/) {
                              int port = grpc_pick_unused_port_or_die();
                              return std::make_unique<LocalTestFixture>(
                                  JoinHostPort("[::1]", port), LOCAL_TCP);
                            }},
      CoreTestConfiguration{
          "Chttp2SslProxy",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_SECURE |
              FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
              FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_FUZZ,
          "foo.test.google.fr",
          [](const ChannelArgs& client_args, const ChannelArgs& server_args) {
            return std::make_unique<SslProxyFixture>(client_args, server_args);
          }},
      CoreTestConfiguration{
          "Chttp2SimpleSslWithOauth2FullstackTls12",
          FEATURE_MASK_IS_SECURE | FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS |
              FEATURE_MASK_DO_NOT_GTEST,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<Oauth2Fixture>(grpc_tls_version::TLS1_2);
          }},
      CoreTestConfiguration{
          "Chttp2SimpleSslWithOauth2FullstackTls13",
          FEATURE_MASK_IS_SECURE | FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DO_NOT_GTEST,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<Oauth2Fixture>(grpc_tls_version::TLS1_3);
          }},
      CoreTestConfiguration{
          "Chttp2SimplSslFullstackTls12",
          FEATURE_MASK_IS_SECURE | FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS |
              FEATURE_MASK_DO_NOT_GTEST,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<SslTlsFixture>(grpc_tls_version::TLS1_2);
          }},
      CoreTestConfiguration{
          "Chttp2SimplSslFullstackTls13",
          FEATURE_MASK_IS_SECURE | FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST |
              FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_GTEST,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<SslTlsFixture>(grpc_tls_version::TLS1_3);
          }},
      CoreTestConfiguration{
          "Chttp2SslCredReloadTls12",
          FEATURE_MASK_IS_SECURE | FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS |
              FEATURE_MASK_DO_NOT_GTEST,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<SslCredReloadFixture>(TLS1_2);
          }},
      CoreTestConfiguration{
          "Chttp2SslCredReloadTls13",
          FEATURE_MASK_IS_SECURE | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST |
              FEATURE_MASK_DO_NOT_GTEST,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<SslCredReloadFixture>(TLS1_3);
          }},
      CoreTestConfiguration{
          // client: certificate watcher provider + async external verifier
          // server: certificate watcher provider + async external verifier
          // extra: TLS 1.3
          "Chttp2CertWatcherProviderAsyncVerifierTls13",
          kH2TLSFeatureMask | FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<TlsFixture>(
                SecurityPrimitives::TlsVersion::V_13,
                SecurityPrimitives::ProviderType::FILE_PROVIDER,
                SecurityPrimitives::VerifierType::EXTERNAL_ASYNC_VERIFIER);
          },
      },
      CoreTestConfiguration{
          // client: certificate watcher provider + hostname verifier
          // server: certificate watcher provider + sync external verifier
          // extra: TLS 1.2
          "Chttp2CertWatcherProviderSyncVerifierTls12",
          kH2TLSFeatureMask | FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<TlsFixture>(
                SecurityPrimitives::TlsVersion::V_12,
                SecurityPrimitives::ProviderType::FILE_PROVIDER,
                SecurityPrimitives::VerifierType::HOSTNAME_VERIFIER);
          },
      },
      CoreTestConfiguration{
          // client: static data provider + sync external verifier
          // server: static data provider + sync external verifier
          // extra: TLS 1.2
          "Chttp2SimpleSslFullstack",
          kH2TLSFeatureMask,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<TlsFixture>(
                SecurityPrimitives::TlsVersion::V_12,
                SecurityPrimitives::ProviderType::STATIC_PROVIDER,
                SecurityPrimitives::VerifierType::EXTERNAL_SYNC_VERIFIER);
          },
      },
      CoreTestConfiguration{
          // client: static data provider + async external verifier
          // server: static data provider + async external verifier
          // extra: TLS 1.3
          "Chttp2StaticProviderAsyncVerifierTls13",
          kH2TLSFeatureMask | FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          "foo.test.google.fr",
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<TlsFixture>(
                SecurityPrimitives::TlsVersion::V_13,
                SecurityPrimitives::ProviderType::STATIC_PROVIDER,
                SecurityPrimitives::VerifierType::EXTERNAL_ASYNC_VERIFIER);
          },
      },
  };
}

}  // namespace grpc_core
