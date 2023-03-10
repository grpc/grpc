//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <functional>
#include <memory>

#include "absl/status/status.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET

#include <fcntl.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

static void create_sockets(int sv[2]) {
  int flags;
  grpc_create_socketpair_if_unix(sv);
  flags = fcntl(sv[0], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);
  GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[0]) == absl::OkStatus());
  GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[1]) == absl::OkStatus());
}

class FdFixture : public CoreTestFixture {
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

  int fd_pair_[2];
};

// All test configurations
static CoreTestConfiguration configs[] = {
    {"chttp2/fd", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
     [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
       return std::make_unique<FdFixture>();
     }},
};

int main(int argc, char** argv) {
  size_t i;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}

#else  // GRPC_POSIX_SOCKET

int main(int /* argc */, char** /* argv */) { return 1; }

#endif  // GRPC_POSIX_SOCKET
