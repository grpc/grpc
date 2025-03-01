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

class CensusFixture : public CoreTestFixture {
 private:
  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    auto* server = grpc_server_create(
        args.Set(GRPC_ARG_ENABLE_CENSUS, true).ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    CHECK(grpc_server_add_http2_port(server, localaddr_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    pre_server_start(server);
    grpc_server_start(server);
    return server;
  }
  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue*) override {
    auto* creds = grpc_insecure_credentials_create();
    auto* client =
        grpc_channel_create(localaddr_.c_str(), creds,
                            args.Set(GRPC_ARG_ENABLE_CENSUS, true).ToC().get());
    grpc_channel_credentials_release(creds);
    return client;
  }
  const std::string localaddr_ =
      JoinHostPort("localhost", grpc_pick_unused_port_or_die());
};

class CompressionFixture : public CoreTestFixture {
 private:
  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    auto* server = grpc_server_create(
        args.SetIfUnset(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM,
                        GRPC_COMPRESS_GZIP)
            .ToC()
            .get(),
        nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    CHECK(grpc_server_add_http2_port(server, localaddr_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    pre_server_start(server);
    grpc_server_start(server);
    return server;
  }
  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue*) override {
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
      JoinHostPort("localhost", grpc_pick_unused_port_or_die());
};

class SockpairWithMinstackFixture : public SockpairFixture {
 public:
  using SockpairFixture::SockpairFixture;

 private:
  ChannelArgs MutateClientArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
  ChannelArgs MutateServerArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
};

class Sockpair1Byte : public SockpairFixture {
 public:
  Sockpair1Byte()
      : SockpairFixture(ChannelArgs()
                            .Set(GRPC_ARG_TCP_READ_CHUNK_SIZE, 1)
                            .Set(GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE, 1)
                            .Set(GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE, 1)) {
    g_fixture_slowdown_factor = 2;
  }
  ~Sockpair1Byte() override { g_fixture_slowdown_factor = 1; }

 private:
  ChannelArgs MutateClientArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
  ChannelArgs MutateServerArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
};

class NoRetryFixture : public InsecureFixture {
 private:
  ChannelArgs MutateClientArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_ENABLE_RETRIES, false);
  }
};

class HttpProxyFilter : public CoreTestFixture {
 public:
  explicit HttpProxyFilter(const ChannelArgs& client_args)
      : proxy_(grpc_end2end_http_proxy_create(client_args.ToC().get())) {}
  ~HttpProxyFilter() override { grpc_end2end_http_proxy_destroy(proxy_); }

 private:
  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    CHECK(
        grpc_server_add_http2_port(server, server_addr_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    pre_server_start(server);
    grpc_server_start(server);
    return server;
  }

  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue*) override {
    // If testing for proxy auth, add credentials to proxy uri
    std::optional<std::string> proxy_auth_str =
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
    CHECK(client);
    return client;
  }

  std::string server_addr_ =
      JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_end2end_http_proxy* proxy_;
};

class ProxyFixture : public CoreTestFixture {
 public:
  ProxyFixture(const ChannelArgs& client_args, const ChannelArgs& server_args)
      : proxy_(grpc_end2end_proxy_create(&proxy_def_, client_args.ToC().get(),
                                         server_args.ToC().get())) {}
  ~ProxyFixture() override { grpc_end2end_proxy_destroy(proxy_); }

 private:
  static grpc_server* CreateProxyServer(const char* port,
                                        const grpc_channel_args* server_args) {
    grpc_server* s = grpc_server_create(server_args, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    CHECK(grpc_server_add_http2_port(s, port, server_creds));
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

  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    CHECK(grpc_server_add_http2_port(
        server, grpc_end2end_proxy_get_server_port(proxy_), server_creds));
    grpc_server_credentials_release(server_creds);
    pre_server_start(server);
    grpc_server_start(server);
    return server;
  }

  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue*) override {
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    auto* client = grpc_channel_create(
        grpc_end2end_proxy_get_client_target(proxy_), creds, args.ToC().get());
    grpc_channel_credentials_release(creds);
    CHECK(client);
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
          "Chttp2Fullstack",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2, nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<InsecureFixture>();
          }},
      CoreTestConfiguration{"Chttp2FullstackCompression",
                            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                FEATURE_MASK_IS_HTTP2 |
                                FEATURE_MASK_DO_NOT_GTEST,
                            nullptr,
                            [](const ChannelArgs&, const ChannelArgs&) {
                              return std::make_unique<CompressionFixture>();
                            }},
      CoreTestConfiguration{
          "Chttp2FullstackNoRetry",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DOES_NOT_SUPPORT_RETRY | FEATURE_MASK_DO_NOT_GTEST,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<NoRetryFixture>();
          }},
      CoreTestConfiguration{"Chttp2FullstackWithCensus",
                            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                FEATURE_MASK_IS_HTTP2 |
                                FEATURE_MASK_DO_NOT_GTEST,
                            nullptr,
                            [](const ChannelArgs&, const ChannelArgs&) {
                              return std::make_unique<CensusFixture>();
                            }},
      CoreTestConfiguration{
          "Chttp2FullstackWithProxy",
          FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
              FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DO_NOT_FUZZ,
          nullptr,
          [](const ChannelArgs& client_args, const ChannelArgs& server_args) {
            return std::make_unique<ProxyFixture>(client_args, server_args);
          }},
      CoreTestConfiguration{
          "Chttp2HttpProxy",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DO_NOT_FUZZ,
          nullptr,
          [](const ChannelArgs& client_args, const ChannelArgs&) {
            return std::make_unique<HttpProxyFilter>(client_args);
          }},
      CoreTestConfiguration{"Chttp2SocketPair",
                            FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_FUZZ |
                                FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
                            nullptr,
                            [](const ChannelArgs&, const ChannelArgs&) {
                              return std::make_unique<SockpairFixture>(
                                  ChannelArgs());
                            }},
      CoreTestConfiguration{
          "Chttp2SocketPair1ByteAtATime",
          FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_1BYTE_AT_A_TIME |
              FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          nullptr,
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<SockpairFixture>(
                ChannelArgs()
                    .Set(GRPC_ARG_TCP_READ_CHUNK_SIZE, 1)
                    .Set(GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE, 1)
                    .Set(GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE, 1));
          }},
      CoreTestConfiguration{
          "Chttp2SocketPairMinstack",
          FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_IS_MINSTACK |
              FEATURE_MASK_DO_NOT_FUZZ,
          nullptr,
          [](const ChannelArgs&, const ChannelArgs&) {
            return std::make_unique<SockpairWithMinstackFixture>(ChannelArgs());
          }},
  };
}

}  // namespace grpc_core
