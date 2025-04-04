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
#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/env.h"
#include "src/core/util/host_port.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/sync.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_oauth2_common.h"
#include "test/core/end2end/fixtures/h2_ssl_cred_reload_fixture.h"
#include "test/core/end2end/fixtures/h2_ssl_tls_common.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/end2end/fixtures/sockpair_fixture.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

// IWYU pragma: no_include <unistd.h>

#ifdef GRPC_POSIX_SOCKET
#include <fcntl.h>

#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#endif

#ifdef GRPC_POSIX_WAKEUP_FD
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#endif

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

namespace grpc_core {

namespace {

uint64_t Rand() {
  struct State {
    Mutex mu;
    absl::BitGen gen ABSL_GUARDED_BY(mu);
  };
  static State* const state = new State;
  MutexLock lock(&state->mu);
  return absl::Uniform<uint64_t>(state->gen);
}

std::atomic<uint64_t> unique{Rand()};

void ProcessAuthFailure(void* state, grpc_auth_context* /*ctx*/,
                        const grpc_metadata* /*md*/, size_t /*md_count*/,
                        grpc_process_auth_metadata_done_cb cb,
                        void* user_data) {
  CHECK_EQ(state, nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

void AddFailAuthCheckIfNeeded(const ChannelArgs& args,
                              grpc_server_credentials* creds) {
  if (args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)) {
    grpc_auth_metadata_processor processor = {ProcessAuthFailure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(creds, processor);
  }
}

}  // namespace

class FakesecFixture : public SecureFixture {
 private:
  grpc_channel_credentials* MakeClientCreds(const ChannelArgs&) override {
    return grpc_fake_transport_security_credentials_create();
  }
  grpc_server_credentials* MakeServerCreds(const ChannelArgs& args) override {
    grpc_server_credentials* fake_ts_creds =
        grpc_fake_transport_security_server_credentials_create();
    AddFailAuthCheckIfNeeded(args, fake_ts_creds);
    return fake_ts_creds;
  }
};

class InsecureCredsFixture : public InsecureFixture {
 private:
  grpc_server_credentials* MakeServerCreds(const ChannelArgs& args) override {
    auto* creds = grpc_insecure_server_credentials_create();
    AddFailAuthCheckIfNeeded(args, creds);
    return creds;
  }
};

class SslProxyFixture : public CoreTestFixture {
 public:
  SslProxyFixture(const ChannelArgs& client_args,
                  const ChannelArgs& server_args)
      : proxy_(grpc_end2end_proxy_create(&proxy_def_, client_args.ToC().get(),
                                         server_args.ToC().get())) {}
  ~SslProxyFixture() override { grpc_end2end_proxy_destroy(proxy_); }

 private:
  static grpc_server* CreateProxyServer(const char* port,
                                        const grpc_channel_args* server_args) {
    grpc_server* s = grpc_server_create(server_args, nullptr);
    std::string server_cert = testing::GetFileContents(SERVER_CERT_PATH);
    std::string server_key = testing::GetFileContents(SERVER_KEY_PATH);
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key.c_str(),
                                                    server_cert.c_str()};
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
        nullptr, &pem_key_cert_pair, 1, 0, nullptr);
    CHECK(grpc_server_add_http2_port(s, port, ssl_creds));
    grpc_server_credentials_release(ssl_creds);
    return s;
  }

  static grpc_channel* CreateProxyClient(const char* target,
                                         const grpc_channel_args* client_args) {
    grpc_channel* channel;
    grpc_channel_credentials* ssl_creds =
        grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
    grpc_arg ssl_name_override = {
        GRPC_ARG_STRING,
        const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
        {const_cast<char*>("foo.test.google.fr")}};
    const grpc_channel_args* new_client_args =
        grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
    channel = grpc_channel_create(target, ssl_creds, new_client_args);
    grpc_channel_credentials_release(ssl_creds);
    {
      ExecCtx exec_ctx;
      grpc_channel_args_destroy(new_client_args);
    }
    return channel;
  }

  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    std::string server_cert = testing::GetFileContents(SERVER_CERT_PATH);
    std::string server_key = testing::GetFileContents(SERVER_KEY_PATH);
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key.c_str(),
                                                    server_cert.c_str()};
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
        nullptr, &pem_key_cert_pair, 1, 0, nullptr);
    if (args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)) {
      grpc_auth_metadata_processor processor = {ProcessAuthFailure, nullptr,
                                                nullptr};
      grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
    }

    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    CHECK(grpc_server_add_http2_port(
        server, grpc_end2end_proxy_get_server_port(proxy_), ssl_creds));
    grpc_server_credentials_release(ssl_creds);
    pre_server_start(server);
    grpc_server_start(server);
    return server;
  }

  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue*) override {
    grpc_channel_credentials* ssl_creds =
        grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
    auto* client = grpc_channel_create(
        grpc_end2end_proxy_get_client_target(proxy_), ssl_creds,
        args.Set(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "foo.test.google.fr")
            .ToC()
            .get());
    CHECK_NE(client, nullptr);
    grpc_channel_credentials_release(ssl_creds);
    return client;
  }
  const grpc_end2end_proxy_def proxy_def_ = {CreateProxyServer,
                                             CreateProxyClient};
  grpc_end2end_proxy* proxy_;
};

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
