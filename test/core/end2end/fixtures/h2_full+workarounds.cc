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

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <grpc/support/workaround_list.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static char* workarounds_arg[GRPC_MAX_WORKAROUND_ID] = {
    const_cast<char*>(GRPC_ARG_WORKAROUND_CRONET_COMPRESSION)};

typedef struct fullstack_fixture_data {
  char* localaddr;
} fullstack_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_fixture_data* ffd = static_cast<fullstack_fixture_data*>(
      gpr_malloc(sizeof(fullstack_fixture_data)));
  memset(&f, 0, sizeof(f));

  gpr_join_host_port(&ffd->localaddr, "localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);

  return f;
}

void chttp2_init_client_fullstack(grpc_end2end_test_fixture* f,
                                  grpc_channel_args* client_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  f->client =
      grpc_insecure_channel_create(ffd->localaddr, client_args, nullptr);
  GPR_ASSERT(f->client);
}

void chttp2_init_server_fullstack(grpc_end2end_test_fixture* f,
                                  grpc_channel_args* server_args) {
  int i;
  grpc_core::ExecCtx exec_ctx;
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  grpc_arg args[GRPC_MAX_WORKAROUND_ID];
  for (i = 0; i < GRPC_MAX_WORKAROUND_ID; i++) {
    args[i].key = workarounds_arg[i];
    args[i].type = GRPC_ARG_INTEGER;
    args[i].value.integer = 1;
  }
  grpc_channel_args* server_args_new =
      grpc_channel_args_copy_and_add(server_args, args, GRPC_MAX_WORKAROUND_ID);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args_new, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  GPR_ASSERT(grpc_server_add_insecure_http2_port(f->server, ffd->localaddr));
  grpc_server_start(f->server);
  grpc_channel_args_destroy(server_args_new);
}

void chttp2_tear_down_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_SUPPORTS_WORKAROUNDS,
     nullptr, chttp2_create_fixture_fullstack, chttp2_init_client_fullstack,
     chttp2_init_server_fullstack, chttp2_tear_down_fullstack},
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
