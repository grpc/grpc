/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET

#include "test/core/end2end/end2end_tests.h"

#include <fcntl.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "test/core/util/test_config.h"

typedef struct {
  int fd_pair[2];
} sp_fixture_data;

static void create_sockets(int sv[2]) {
  int flags;
  grpc_create_socketpair_if_unix(sv);
  flags = fcntl(sv[0], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
  flags = fcntl(sv[1], F_GETFL, 0);
  GPR_ASSERT(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) == 0);
  GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[0]) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_set_socket_no_sigpipe_if_possible(sv[1]) == GRPC_ERROR_NONE);
}

static grpc_end2end_test_fixture chttp2_create_fixture_socketpair(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  sp_fixture_data* fixture_data =
      static_cast<sp_fixture_data*>(gpr_malloc(sizeof(*fixture_data)));

  grpc_end2end_test_fixture f;
  memset(&f, 0, sizeof(f));
  f.fixture_data = fixture_data;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);

  create_sockets(fixture_data->fd_pair);

  return f;
}

static void chttp2_init_client_socketpair(grpc_end2end_test_fixture* f,
                                          grpc_channel_args* client_args) {
  grpc_core::ExecCtx exec_ctx;
  sp_fixture_data* sfd = static_cast<sp_fixture_data*>(f->fixture_data);

  GPR_ASSERT(!f->client);
  f->client = grpc_insecure_channel_create_from_fd(
      "fixture_client", sfd->fd_pair[0], client_args);
  GPR_ASSERT(f->client);
}

static void chttp2_init_server_socketpair(grpc_end2end_test_fixture* f,
                                          grpc_channel_args* server_args) {
  grpc_core::ExecCtx exec_ctx;
  sp_fixture_data* sfd = static_cast<sp_fixture_data*>(f->fixture_data);
  GPR_ASSERT(!f->server);
  f->server = grpc_server_create(server_args, nullptr);
  GPR_ASSERT(f->server);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_start(f->server);

  grpc_server_add_insecure_channel_from_fd(f->server, nullptr, sfd->fd_pair[1]);
}

static void chttp2_tear_down_socketpair(grpc_end2end_test_fixture* f) {
  gpr_free(f->fixture_data);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fd", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
     chttp2_create_fixture_socketpair, chttp2_init_client_socketpair,
     chttp2_init_server_socketpair, chttp2_tear_down_socketpair},
};

int main(int argc, char** argv) {
  size_t i;

  grpc_test_init(argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
