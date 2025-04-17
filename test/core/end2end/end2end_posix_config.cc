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

}  // namespace

#ifdef GRPC_POSIX_SOCKET
class FdFixture : public CoreTestFixture {
 public:
  FdFixture() { create_sockets(fd_pair_); }

 private:
  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    ExecCtx exec_ctx;
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    pre_server_start(server);
    grpc_server_start(server);
    grpc_server_credentials* creds = grpc_insecure_server_credentials_create();
    grpc_server_add_channel_from_fd(server, fd_pair_[1], creds);
    grpc_server_credentials_release(creds);
    return server;
  }
  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue*) override {
    ExecCtx exec_ctx;
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
    CHECK_EQ(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK), 0);
    flags = fcntl(sv[1], F_GETFL, 0);
    CHECK_EQ(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK), 0);
    CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[0]) == absl::OkStatus());
    CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[1]) == absl::OkStatus());
  }

  int fd_pair_[2];
};
#endif

#ifdef GRPC_POSIX_WAKEUP_FD
class InsecureFixtureWithPipeForWakeupFd : public InsecureFixture {
 public:
  InsecureFixtureWithPipeForWakeupFd()
      : old_value_(std::exchange(grpc_allow_specialized_wakeup_fd, 0)) {}

  ~InsecureFixtureWithPipeForWakeupFd() override {
    grpc_allow_specialized_wakeup_fd = old_value_;
  }

 private:
  const int old_value_;
};
#endif

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
#ifdef GRPC_POSIX_SOCKET
      CoreTestConfiguration{"Chttp2Fd",
                            FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_FUZZ |
                                FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
                            nullptr,
                            [](const ChannelArgs&, const ChannelArgs&) {
                              return std::make_unique<FdFixture>();
                            }},
#endif
#ifdef GPR_LINUX
      CoreTestConfiguration{
          "Chttp2FullstackLocalAbstractUdsPercentEncoded",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
            return std::make_unique<LocalTestFixture>(
                absl::StrFormat(
                    "unix-abstract:grpc_fullstack_test.%%00.%d.%" PRId64
                    ".%" PRId32 ".%" PRId64 ".%" PRId64,
                    getpid(), now.tv_sec, now.tv_nsec,
                    unique.fetch_add(1, std::memory_order_relaxed), Rand()),
                UDS);
          }},
#endif
#ifdef GRPC_HAVE_UNIX_SOCKET
      CoreTestConfiguration{
          "Chttp2FullstackLocalUdsPercentEncoded",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
            return std::make_unique<LocalTestFixture>(
                absl::StrFormat("unix:%s"
                                "grpc_fullstack_test.%%25.%d.%" PRId64
                                ".%" PRId32 ".%" PRId64 ".%" PRId64,
                                temp_dir, getpid(), now.tv_sec, now.tv_nsec,
                                unique.fetch_add(1, std::memory_order_relaxed),
                                Rand()),
                UDS);
          }},
      CoreTestConfiguration{
          "Chttp2FullstackLocalUds",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
              FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
              FEATURE_MASK_IS_HTTP2 | FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
            return std::make_unique<LocalTestFixture>(
                absl::StrFormat("unix:%s"
                                "grpc_fullstack_test.%d.%" PRId64 ".%" PRId32
                                ".%" PRId64 ".%" PRId64,
                                temp_dir, getpid(), now.tv_sec, now.tv_nsec,
                                unique.fetch_add(1, std::memory_order_relaxed),
                                Rand()),
                UDS);
          }},
#endif
#ifdef GPR_LINUX
      CoreTestConfiguration{
          "Chttp2FullstackUdsAbstractNamespace",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          nullptr,
          [](const ChannelArgs&, const ChannelArgs&) {
            gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
            return std::make_unique<InsecureFixture>(absl::StrFormat(
                "unix-abstract:grpc_fullstack_test.%d.%" PRId64 ".%" PRId32
                ".%" PRId64,
                getpid(), now.tv_sec, now.tv_nsec,
                unique.fetch_add(1, std::memory_order_relaxed)));
          }},
#endif
#ifdef GRPC_HAVE_UNIX_SOCKET
      CoreTestConfiguration{
          "Chttp2FullstackUds",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DO_NOT_FUZZ,
          nullptr,
          [](const ChannelArgs&, const ChannelArgs&) {
            gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
            return std::make_unique<InsecureFixture>(absl::StrFormat(
                "unix:%s"
                "grpc_fullstack_test.%d.%" PRId64 ".%" PRId32 ".%" PRId64
                ".%" PRId64,
                temp_dir, getpid(), now.tv_sec, now.tv_nsec,
                unique.fetch_add(1, std::memory_order_relaxed), Rand()));
          }},
#endif
#ifdef GRPC_POSIX_WAKEUP_FD
      CoreTestConfiguration{
          "Chttp2FullstackWithPipeWakeup",
          FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DO_NOT_FUZZ |
              FEATURE_MASK_EXCLUDE_FROM_EXPERIMENT_RUNS,
          nullptr,
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            return std::make_unique<InsecureFixtureWithPipeForWakeupFd>();
          }},
#endif
  };
}

}  // namespace grpc_core
