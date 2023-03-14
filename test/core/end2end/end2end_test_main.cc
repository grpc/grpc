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

#include <memory>
#include <vector>

#include "end2end_tests.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_oauth2_common.h"
#include "test/core/end2end/fixtures/h2_ssl_tls_common.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/end2end/fixtures/inproc_fixture.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/end2end/fixtures/sockpair_fixture.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_POSIX_SOCKET
#include <fcntl.h>
#include <string.h>

#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#endif

namespace grpc_core {

namespace {

std::atomic<int> unique{0};

void ProcessAuthFailure(void* state, grpc_auth_context* /*ctx*/,
                        const grpc_metadata* /*md*/, size_t /*md_count*/,
                        grpc_process_auth_metadata_done_cb cb,
                        void* user_data) {
  GPR_ASSERT(state == nullptr);
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

class CensusFixture : public grpc_core::CoreTestFixture {
 private:
  grpc_server* MakeServer(const grpc_core::ChannelArgs& args) override {
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    auto* server = grpc_server_create(
        args.Set(GRPC_ARG_ENABLE_CENSUS, true).ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    GPR_ASSERT(
        grpc_server_add_http2_port(server, localaddr_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server);
    return server;
  }
  grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) override {
    auto* creds = grpc_insecure_credentials_create();
    auto* client =
        grpc_channel_create(localaddr_.c_str(), creds,
                            args.Set(GRPC_ARG_ENABLE_CENSUS, true).ToC().get());
    grpc_channel_credentials_release(creds);
    return client;
  }
  const std::string localaddr_ =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
};

class CompressionFixture : public grpc_core::CoreTestFixture {
 private:
  grpc_server* MakeServer(const grpc_core::ChannelArgs& args) override {
    auto* server = grpc_server_create(
        args.SetIfUnset(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM,
                        GRPC_COMPRESS_GZIP)
            .ToC()
            .get(),
        nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GPR_ASSERT(
        grpc_server_add_http2_port(server, localaddr_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server);
    return server;
  }
  grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) override {
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    auto* client = grpc_channel_create(
        localaddr_.c_str(), creds,
        args.SetIfUnset(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM,
                        GRPC_COMPRESS_GZIP)
            .ToC()
            .get());
    grpc_channel_credentials_release(creds);
    return client;
  }

  std::string localaddr_ =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
};

class FakesecFixture : public SecureFixture {
 private:
  grpc_channel_credentials* MakeClientCreds(
      const grpc_core::ChannelArgs&) override {
    return grpc_fake_transport_security_credentials_create();
  }
  grpc_server_credentials* MakeServerCreds(
      const grpc_core::ChannelArgs& args) override {
    grpc_server_credentials* fake_ts_creds =
        grpc_fake_transport_security_server_credentials_create();
    AddFailAuthCheckIfNeeded(args, fake_ts_creds);
    return fake_ts_creds;
  }
};

class InsecureCredsFixture : public InsecureFixture {
 private:
  grpc_server_credentials* MakeServerCreds(
      const grpc_core::ChannelArgs& args) override {
    auto* creds = grpc_insecure_server_credentials_create();
    AddFailAuthCheckIfNeeded(args, creds);
    return creds;
  }
};

class SockpairWithMinstackFixture : public SockpairFixture {
 public:
  using SockpairFixture::SockpairFixture;

 private:
  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
  grpc_core::ChannelArgs MutateServerArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
};

class Sockpair1Byte : public SockpairFixture {
 public:
  Sockpair1Byte()
      : SockpairFixture(grpc_core::ChannelArgs()
                            .Set(GRPC_ARG_TCP_READ_CHUNK_SIZE, 1)
                            .Set(GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE, 1)
                            .Set(GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE, 1)) {
    g_fixture_slowdown_factor = 2;
  }
  ~Sockpair1Byte() { g_fixture_slowdown_factor = 1; }

 private:
  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
  grpc_core::ChannelArgs MutateServerArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
};

#ifdef GRPC_POSIX_SOCKET

class FdFixture : public grpc_core::CoreTestFixture {
 public:
  FdFixture() { create_sockets(fd_pair_); }

 private:
  grpc_server* MakeServer(const grpc_core::ChannelArgs& args) override {
    grpc_core::ExecCtx exec_ctx;
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    grpc_server_start(server);
    grpc_server_credentials* creds = grpc_insecure_server_credentials_create();
    grpc_server_add_channel_from_fd(server, fd_pair_[1], creds);
    grpc_server_credentials_release(creds);
    return server;
  }
  grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) override {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    auto* client = grpc_channel_create_from_fd("fixture_client", fd_pair_[0],
                                               creds, args.ToC().get());
    grpc_channel_credentials_release(creds);
    return client;
  }

  static void create_sockets(int sv[2]) {
    int flags;
    grpc_create_socketpair_if_unix(sv);
    flags = fcntl(sv[0], F_GETFL, 0);
    GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
    flags = fcntl(sv[1], F_GETFL, 0);
    GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);
    GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[0]) ==
               absl::OkStatus());
    GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[1]) ==
               absl::OkStatus());
  }

  int fd_pair_[2];
};
#endif

class NoRetryFixture : public InsecureFixture {
 private:
  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_ENABLE_RETRIES, false);
  }
};

class HttpProxyFilter : public grpc_core::CoreTestFixture {
 public:
  explicit HttpProxyFilter(const grpc_core::ChannelArgs& client_args)
      : proxy_(grpc_end2end_http_proxy_create(client_args.ToC().get())) {}
  ~HttpProxyFilter() override {
    // Need to shut down the proxy users before closing the proxy (otherwise we
    // become stuck).
    ShutdownClient();
    ShutdownServer();
    grpc_end2end_http_proxy_destroy(proxy_);
  }

 private:
  grpc_server* MakeServer(const grpc_core::ChannelArgs& args) override {
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GPR_ASSERT(
        grpc_server_add_http2_port(server, server_addr_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server);
    return server;
  }

  grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) override {
    // If testing for proxy auth, add credentials to proxy uri
    absl::optional<std::string> proxy_auth_str =
        args.GetOwnedString(GRPC_ARG_HTTP_PROXY_AUTH_CREDS);
    std::string proxy_uri;
    if (!proxy_auth_str.has_value()) {
      proxy_uri = absl::StrFormat(
          "http://%s", grpc_end2end_http_proxy_get_proxy_name(proxy_));
    } else {
      proxy_uri =
          absl::StrFormat("http://%s@%s", proxy_auth_str->c_str(),
                          grpc_end2end_http_proxy_get_proxy_name(proxy_));
    }
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    auto* client = grpc_channel_create(
        server_addr_.c_str(), creds,
        args.Set(GRPC_ARG_HTTP_PROXY, proxy_uri).ToC().get());
    grpc_channel_credentials_release(creds);
    GPR_ASSERT(client);
    return client;
  }

  std::string server_addr_ =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_end2end_http_proxy* proxy_;
};

class ProxyFixture : public grpc_core::CoreTestFixture {
 public:
  ProxyFixture(const grpc_core::ChannelArgs& client_args,
               const grpc_core::ChannelArgs& server_args)
      : proxy_(grpc_end2end_proxy_create(&proxy_def_, client_args.ToC().get(),
                                         server_args.ToC().get())) {}
  ~ProxyFixture() override { grpc_end2end_proxy_destroy(proxy_); }

 private:
  static grpc_server* CreateProxyServer(const char* port,
                                        const grpc_channel_args* server_args) {
    grpc_server* s = grpc_server_create(server_args, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GPR_ASSERT(grpc_server_add_http2_port(s, port, server_creds));
    grpc_server_credentials_release(server_creds);
    return s;
  }

  static grpc_channel* CreateProxyClient(const char* target,
                                         const grpc_channel_args* client_args) {
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    grpc_channel* channel = grpc_channel_create(target, creds, client_args);
    grpc_channel_credentials_release(creds);
    return channel;
  }

  grpc_server* MakeServer(const grpc_core::ChannelArgs& args) override {
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GPR_ASSERT(grpc_server_add_http2_port(
        server, grpc_end2end_proxy_get_server_port(proxy_), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server);
    return server;
  }

  grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) override {
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    auto* client = grpc_channel_create(
        grpc_end2end_proxy_get_client_target(proxy_), creds, args.ToC().get());
    grpc_channel_credentials_release(creds);
    GPR_ASSERT(client);
    return client;
  }
  const grpc_end2end_proxy_def proxy_def_ = {CreateProxyServer,
                                             CreateProxyClient};
  grpc_end2end_proxy* proxy_;
};

const char* NameFromConfig(
    const ::testing::TestParamInfo<const CoreTestConfiguration*>& config) {
  return config.param->name;
}

const NoDestruct<std::vector<CoreTestConfiguration>> all_configs{std::vector<
    CoreTestConfiguration>{
#ifdef GRPC_POSIX_SOCKET
    CoreTestConfiguration{
        "Chttp2Fd", FEATURE_MASK_IS_HTTP2, nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<FdFixture>();
        }},
#endif
    CoreTestConfiguration{
        "Chttp2FakeSecurityFullstack",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE |
            FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<FakesecFixture>();
        }},
    CoreTestConfiguration{
        "Chttp2Fullstack",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2, nullptr,
        [](const ChannelArgs& /*client_args*/,
           const ChannelArgs& /*server_args*/) {
          return std::make_unique<InsecureFixture>();
        }},
    CoreTestConfiguration{
        "Chttp2FullstackCompression",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2, nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<CompressionFixture>();
        }},
    CoreTestConfiguration{
        "Chttp2FullstackLocalAbstractUdsPercentEncoded",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS | FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs& /*client_args*/,
           const grpc_core::ChannelArgs& /*server_args*/) {
          gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
          return std::make_unique<LocalTestFixture>(
              absl::StrFormat(
                  "unix-abstract:grpc_fullstack_test.%%00.%d.%" PRId64
                  ".%" PRId32 ".%d",
                  getpid(), now.tv_sec, now.tv_nsec,
                  unique.fetch_add(1, std::memory_order_relaxed)),
              UDS);
        }},
    CoreTestConfiguration{
        "Chttp2FullstackLocalIpv4",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS | FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs& /*client_args*/,
           const grpc_core::ChannelArgs& /*server_args*/) {
          int port = grpc_pick_unused_port_or_die();
          return std::make_unique<LocalTestFixture>(
              grpc_core::JoinHostPort("127.0.0.1", port), LOCAL_TCP);
        }},
    CoreTestConfiguration{
        "Chttp2FullstackLocalIpv6",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS | FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs& /*client_args*/,
           const grpc_core::ChannelArgs& /*server_args*/) {
          int port = grpc_pick_unused_port_or_die();
          return std::make_unique<LocalTestFixture>(
              grpc_core::JoinHostPort("[::1]", port), LOCAL_TCP);
        }},
    CoreTestConfiguration{
        "Chttp2FullstackLocalUdsPercentEncoded",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS | FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs& /*client_args*/,
           const grpc_core::ChannelArgs& /*server_args*/) {
          gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
          return std::make_unique<LocalTestFixture>(
              absl::StrFormat("unix:/tmp/grpc_fullstack_test.%%25.%d.%" PRId64
                              ".%" PRId32 ".%d",
                              getpid(), now.tv_sec, now.tv_nsec,
                              unique.fetch_add(1, std::memory_order_relaxed)),
              UDS);
        }},
    CoreTestConfiguration{
        "Chttp2FullstackLocalUds",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS | FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs& /*client_args*/,
           const grpc_core::ChannelArgs& /*server_args*/) {
          gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
          return std::make_unique<LocalTestFixture>(
              absl::StrFormat("unix:/tmp/grpc_fullstack_test.%d.%" PRId64
                              ".%" PRId32 ".%d",
                              getpid(), now.tv_sec, now.tv_nsec,
                              unique.fetch_add(1, std::memory_order_relaxed)),
              UDS);
        }},
    CoreTestConfiguration{"Chttp2FullstackNoRetry",
                          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                              FEATURE_MASK_IS_HTTP2 |
                              FEATURE_MASK_DOES_NOT_SUPPORT_RETRY,
                          nullptr,
                          [](const ChannelArgs& /*client_args*/,
                             const ChannelArgs& /*server_args*/) {
                            return std::make_unique<NoRetryFixture>();
                          }},
    CoreTestConfiguration{
        "Chttp2FullstackWithCensus",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2, nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<CensusFixture>();
        }},
    CoreTestConfiguration{
        "Chttp2FullstackWithProxy",
        FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs& client_args,
           const grpc_core::ChannelArgs& server_args) {
          return std::make_unique<ProxyFixture>(client_args, server_args);
        }},
    CoreTestConfiguration{
        "Chttp2HttpProxy",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2, nullptr,
        [](const grpc_core::ChannelArgs& client_args,
           const grpc_core::ChannelArgs&) {
          return std::make_unique<HttpProxyFilter>(client_args);
        }},
    CoreTestConfiguration{
        "Chttp2InsecureCredentials",
        FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE |
            FEATURE_MASK_IS_HTTP2,
        nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<InsecureCredsFixture>();
        },
    },
    CoreTestConfiguration{
        "Chttp2SimpleSslWithOauth2FullstackTls12",
        FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2,
        "foo.test.google.fr",
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<Oauth2Fixture>(grpc_tls_version::TLS1_2);
        }},
    CoreTestConfiguration{
        "Chttp2SimpleSslWithOauth2FullstackTls13",
        FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2,
        "foo.test.google.fr",
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<Oauth2Fixture>(grpc_tls_version::TLS1_3);
        }},
    CoreTestConfiguration{
        "Chttp2SimplSslFullstackTls12",
        FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL,
        "foo.test.google.fr",
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<SslTlsFixture>(grpc_tls_version::TLS1_2);
        }},
    CoreTestConfiguration{
        "Chttp2SimplSslFullstackTls13",
        FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
            FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST,
        "foo.test.google.fr",
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<SslTlsFixture>(grpc_tls_version::TLS1_3);
        }},
    CoreTestConfiguration{
        "Chttp2SocketPair", FEATURE_MASK_IS_HTTP2, nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<SockpairFixture>(grpc_core::ChannelArgs());
        }},
    CoreTestConfiguration{
        "Chttp2SocketPair1ByteAtATime",
        FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_1BYTE_AT_A_TIME, nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<SockpairFixture>(
              grpc_core::ChannelArgs()
                  .Set(GRPC_ARG_TCP_READ_CHUNK_SIZE, 1)
                  .Set(GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE, 1)
                  .Set(GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE, 1));
        }},
    CoreTestConfiguration{
        "Chttp2SocketPairMinstack",
        FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES,
        nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<SockpairWithMinstackFixture>(
              grpc_core::ChannelArgs());
        }},
    CoreTestConfiguration{
        "Inproc",
        FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING,
        nullptr,
        [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
          return std::make_unique<InprocFixture>();
        },
    }}};  // namespace grpc_core

std::vector<const CoreTestConfiguration*> QueryConfigs(uint32_t enforce_flags,
                                                       uint32_t exclude_flags) {
  std::vector<const CoreTestConfiguration*> out;
  for (const CoreTestConfiguration& config : *all_configs) {
    if ((config.feature_mask & enforce_flags) == enforce_flags &&
        (config.feature_mask & exclude_flags) == 0) {
      out.push_back(&config);
    }
  }
  return out;
}

INSTANTIATE_TEST_SUITE_P(CoreEnd2endTests, CoreEnd2endTest,
                         ::testing::ValuesIn(QueryConfigs(0, 0)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(
    CoreLargeSendTests, CoreLargeSendTest,
    ::testing::ValuesIn(QueryConfigs(0, FEATURE_MASK_1BYTE_AT_A_TIME)),
    NameFromConfig);

INSTANTIATE_TEST_SUITE_P(CoreDeadlineTests, CoreDeadlineTest,
                         ::testing::ValuesIn(QueryConfigs(
                             0, FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(
    CoreClientChannelTests, CoreClientChannelTest,
    ::testing::ValuesIn(QueryConfigs(FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL, 0)),
    NameFromConfig);

INSTANTIATE_TEST_SUITE_P(
    HpackSizeTests, HpackSizeTest,
    ::testing::ValuesIn(QueryConfigs(FEATURE_MASK_IS_HTTP2,
                                     FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
                                         FEATURE_MASK_ENABLES_TRACES)),
    NameFromConfig);

INSTANTIATE_TEST_SUITE_P(
    RetryTests, RetryTest,
    ::testing::ValuesIn(QueryConfigs(FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL,
                                     FEATURE_MASK_DOES_NOT_SUPPORT_RETRY)),
    NameFromConfig);

INSTANTIATE_TEST_SUITE_P(WriteBufferingTests, WriteBufferingTest,
                         ::testing::ValuesIn(QueryConfigs(
                             0, FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(Http2Tests, Http2Test,
                         ::testing::ValuesIn(QueryConfigs(FEATURE_MASK_IS_HTTP2,
                                                          0)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(RetryHttp2Tests, RetryHttp2Test,
                         ::testing::ValuesIn(QueryConfigs(
                             FEATURE_MASK_IS_HTTP2 |
                                 FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL,
                             FEATURE_MASK_DOES_NOT_SUPPORT_RETRY |
                                 FEATURE_MASK_SUPPORTS_REQUEST_PROXYING)),
                         NameFromConfig);

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // TODO(ctiller): make this per fixture?
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path,
                        Oauth2Fixture::CaCertPath());
  return RUN_ALL_TESTS();
}
