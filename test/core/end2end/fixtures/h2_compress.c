/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct fullstack_compression_fixture_data {
  char *localaddr;
  grpc_channel_args *client_args_compression;
  grpc_channel_args *server_args_compression;
} fullstack_compression_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack_compression(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_compression_fixture_data *ffd =
      gpr_malloc(sizeof(fullstack_compression_fixture_data));
  memset(ffd, 0, sizeof(fullstack_compression_fixture_data));

  gpr_join_host_port(&ffd->localaddr, "localhost", port);

  memset(&f, 0, sizeof(f));
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create(NULL);

  return f;
}

void chttp2_init_client_fullstack_compression(grpc_end2end_test_fixture *f,
                                              grpc_channel_args *client_args) {
  fullstack_compression_fixture_data *ffd = f->fixture_data;
  if (ffd->client_args_compression != NULL) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_args_destroy(&exec_ctx, ffd->client_args_compression);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  ffd->client_args_compression = grpc_channel_args_set_compression_algorithm(
      client_args, GRPC_COMPRESS_GZIP);
  f->client = grpc_insecure_channel_create(ffd->localaddr,
                                           ffd->client_args_compression, NULL);
}

void chttp2_init_server_fullstack_compression(grpc_end2end_test_fixture *f,
                                              grpc_channel_args *server_args) {
  fullstack_compression_fixture_data *ffd = f->fixture_data;
  if (ffd->server_args_compression != NULL) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_args_destroy(&exec_ctx, ffd->server_args_compression);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  ffd->server_args_compression = grpc_channel_args_set_compression_algorithm(
      server_args, GRPC_COMPRESS_GZIP);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(ffd->server_args_compression, NULL);
  grpc_server_register_completion_queue(f->server, f->cq, NULL);
  GPR_ASSERT(grpc_server_add_insecure_http2_port(f->server, ffd->localaddr));
  grpc_server_start(f->server);
}

void chttp2_tear_down_fullstack_compression(grpc_end2end_test_fixture *f) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  fullstack_compression_fixture_data *ffd = f->fixture_data;
  grpc_channel_args_destroy(&exec_ctx, ffd->client_args_compression);
  grpc_channel_args_destroy(&exec_ctx, ffd->server_args_compression);
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
  grpc_exec_ctx_finish(&exec_ctx);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack_compression", FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
                                         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     chttp2_create_fixture_fullstack_compression,
     chttp2_init_client_fullstack_compression,
     chttp2_init_server_fullstack_compression,
     chttp2_tear_down_fullstack_compression},
};

int main(int argc, char **argv) {
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
