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
#include "test/core/end2end/end2end_tests.h"
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

const char* NameFromConfig(
    const ::testing::TestParamInfo<const CoreTestConfiguration*>& config) {
  return config.param->name;
}

const NoDestruct<std::vector<CoreTestConfiguration>> all_configs{
    std::vector<CoreTestConfiguration>{
#ifdef GRPC_POSIX_SOCKET
        CoreTestConfiguration{
            "Chttp2Fd", FEATURE_MASK_IS_HTTP2, nullptr,
            [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
              return std::make_unique<FdFixture>();
            }},
#endif
        CoreTestConfiguration{"Chttp2Fullstack",
                              FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
                                  FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                  FEATURE_MASK_IS_HTTP2,
                              nullptr,
                              [](const ChannelArgs& /*client_args*/,
                                 const ChannelArgs& /*server_args*/) {
                                return std::make_unique<InsecureFixture>();
                              }},
        CoreTestConfiguration{
            "Chttp2FullstackCompression",
            FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
                FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2,
            nullptr,
            [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
              return std::make_unique<CompressionFixture>();
            }},
        CoreTestConfiguration{"Chttp2FullstackNoRetry",
                              FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
                                  FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                  FEATURE_MASK_IS_HTTP2,
                              nullptr,
                              [](const ChannelArgs& /*client_args*/,
                                 const ChannelArgs& /*server_args*/) {
                                return std::make_unique<NoRetryFixture>();
                              }},
        CoreTestConfiguration{
            "Chttp2SocketPair", FEATURE_MASK_IS_HTTP2, nullptr,
            [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
              return std::make_unique<SockpairFixture>(
                  grpc_core::ChannelArgs());
            }},
        CoreTestConfiguration{
            "Chttp2SocketPair1ByteAtATime", FEATURE_MASK_IS_HTTP2, nullptr,
            [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
              return std::make_unique<SockpairFixture>(
                  grpc_core::ChannelArgs()
                      .Set(GRPC_ARG_TCP_READ_CHUNK_SIZE, 1)
                      .Set(GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE, 1)
                      .Set(GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE, 1));
            }},
        CoreTestConfiguration{
            "Chttp2SocketPairMinstack",
            FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES |
                FEATURE_MASK_IS_MINSTACK,
            nullptr,
            [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
              return std::make_unique<SockpairWithMinstackFixture>(
                  grpc_core::ChannelArgs());
            }}}};  // namespace grpc_core

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

INSTANTIATE_TEST_SUITE_P(CoreDeadlineTests, CoreDeadlineTest,
                         ::testing::ValuesIn(QueryConfigs(
                             0, FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(
    CoreClientChannelTests, CoreClientChannelTest,
    ::testing::ValuesIn(QueryConfigs(FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL, 0)),
    NameFromConfig);

INSTANTIATE_TEST_SUITE_P(CoreEnd2endTests, CoreDelayedConnectionTest,
                         ::testing::ValuesIn(QueryConfigs(
                             FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION, 0)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(
    HpackSizeTests, HpackSizeTest,
    ::testing::ValuesIn(QueryConfigs(FEATURE_MASK_IS_HTTP2,
                                     FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
                                         FEATURE_MASK_ENABLES_TRACES)),
    NameFromConfig);

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
