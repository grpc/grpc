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
#include "src/core/ext/transport/chaotic_good/chaotic_good.h"
#include "src/core/ext/transport/chaotic_good/client/chaotic_good_connector.h"
#include "src/core/ext/transport/chaotic_good/server/chaotic_good_server.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/transport/endpoint_transport.h"
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
#include "test/core/end2end/fixtures/inproc_fixture.h"
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

template <typename SecureFixtureImpl>
class ChaoticGoodFixture : public SecureFixtureImpl {
 public:
  explicit ChaoticGoodFixture(int data_connections = 1, int chunk_size = 0,
                              std::string localaddr = JoinHostPort(
                                  "localhost", grpc_pick_unused_port_or_die()))
      : data_connections_(data_connections),
        chunk_size_(chunk_size),
        localaddr_(std::move(localaddr)) {}

 protected:
  const std::string& localaddr() const { return localaddr_; }

 private:
  ChannelArgs MutateClientArgs(ChannelArgs args) override {
    return SecureFixtureImpl::MutateClientArgs(args)
        .Set(GRPC_ARG_CHAOTIC_GOOD_MAX_RECV_CHUNK_SIZE, chunk_size_)
        .Set(GRPC_ARG_CHAOTIC_GOOD_MAX_SEND_CHUNK_SIZE, chunk_size_)
        .SetIfUnset(GRPC_ARG_ENABLE_RETRIES, IsRetryInCallv3Enabled())
        .Set(GRPC_ARG_PREFERRED_TRANSPORT_PROTOCOLS,
             chaotic_good::WireFormatPreferences());
  }

  ChannelArgs MutateServerArgs(ChannelArgs args) override {
    return SecureFixtureImpl::MutateServerArgs(args)
        .Set(GRPC_ARG_CHAOTIC_GOOD_DATA_CONNECTIONS, data_connections_)
        .Set(GRPC_ARG_CHAOTIC_GOOD_MAX_RECV_CHUNK_SIZE, chunk_size_)
        .Set(GRPC_ARG_CHAOTIC_GOOD_MAX_SEND_CHUNK_SIZE, chunk_size_)
        .Set(GRPC_ARG_PREFERRED_TRANSPORT_PROTOCOLS,
             chaotic_good::WireFormatPreferences());
  }

  int data_connections_;
  int chunk_size_;
  std::string localaddr_;
};

class ChaoticGoodSingleConnectionFixture final
    : public ChaoticGoodFixture<InsecureFixture> {
 public:
  ChaoticGoodSingleConnectionFixture() : ChaoticGoodFixture(0) {}
};

class ChaoticGoodSecureSingleConnectionFixture final
    : public ChaoticGoodFixture<SslTlsFixture1_3> {
 public:
  ChaoticGoodSecureSingleConnectionFixture() : ChaoticGoodFixture(0) {}
};

class ChaoticGoodManyConnectionFixture final
    : public ChaoticGoodFixture<InsecureFixture> {
 public:
  ChaoticGoodManyConnectionFixture() : ChaoticGoodFixture(16) {}
};

class ChaoticGoodSecureManyConnectionFixture final
    : public ChaoticGoodFixture<SslTlsFixture1_3> {
 public:
  ChaoticGoodSecureManyConnectionFixture() : ChaoticGoodFixture(16) {}
};

class ChaoticGoodOneByteChunkFixture final
    : public ChaoticGoodFixture<InsecureFixture> {
 public:
  ChaoticGoodOneByteChunkFixture() : ChaoticGoodFixture(1, 1) {}
};

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  if (!IsEventEngineClientEnabled() || !IsEventEngineListenerEnabled()) {
    return {};
  }

  std::vector<CoreTestConfiguration> config{
      CoreTestConfiguration{
          "ChaoticGoodFullStack",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING |
              FEATURE_MASK_IS_CALL_V3,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<ChaoticGoodFixture<InsecureFixture>>();
          }},
      CoreTestConfiguration{
          "ChaoticGoodManyConnections",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_DOES_NOT_SUPPORT_RETRY |
              FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING |
              FEATURE_MASK_IS_CALL_V3,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<ChaoticGoodManyConnectionFixture>();
          }},
      CoreTestConfiguration{
          "ChaoticGoodSingleConnection",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_DOES_NOT_SUPPORT_RETRY |
              FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING |
              FEATURE_MASK_IS_CALL_V3 | FEATURE_MASK_DO_NOT_GTEST,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<ChaoticGoodSingleConnectionFixture>();
          }},
      CoreTestConfiguration{
          "ChaoticGoodOneByteChunk",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_1BYTE_AT_A_TIME |
              FEATURE_MASK_DOES_NOT_SUPPORT_RETRY |
              FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING |
              FEATURE_MASK_IS_CALL_V3 | FEATURE_MASK_DO_NOT_GTEST,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<ChaoticGoodOneByteChunkFixture>();
          }},
  };

  if (IsEventEngineSecureEndpointEnabled() &&
      IsChaoticGoodFramingLayerEnabled()) {
    std::vector<CoreTestConfiguration> secure_config{
        CoreTestConfiguration{
            "ChaoticGoodSecureFullStack",
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING |
                FEATURE_MASK_IS_CALL_V3,
            "foo.test.google.fr",
            [](const ChannelArgs& /*client_args*/,
               const ChannelArgs& /*server_args*/) {
              return std::make_unique<ChaoticGoodFixture<SslTlsFixture1_3>>();
            }},
        CoreTestConfiguration{
            "ChaoticGoodSecureManyConnections",
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DOES_NOT_SUPPORT_RETRY |
                FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING |
                FEATURE_MASK_IS_CALL_V3,
            "foo.test.google.fr",
            [](const ChannelArgs& /*client_args*/,
               const ChannelArgs& /*server_args*/) {
              return std::make_unique<ChaoticGoodSecureManyConnectionFixture>();
            }},
        CoreTestConfiguration{
            "ChaoticGoodSecureSingleConnection",
            FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                FEATURE_MASK_DOES_NOT_SUPPORT_RETRY |
                FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING |
                FEATURE_MASK_IS_CALL_V3 | FEATURE_MASK_DO_NOT_GTEST,
            "foo.test.google.fr",
            [](const ChannelArgs& /*client_args*/,
               const ChannelArgs& /*server_args*/) {
              return std::make_unique<
                  ChaoticGoodSecureSingleConnectionFixture>();
            }},
    };
    for (auto& c : secure_config) {
      config.emplace_back(std::move(c));
    }
  }
  return config;
}

}  // namespace grpc_core
