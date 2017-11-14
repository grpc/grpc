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
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

/* chttp2 transport that is immediately available (used for testing
   connected_channel without a client_channel */

static void server_setup_transport(void* ts, grpc_transport* transport) {
  grpc_end2end_test_fixture* f = static_cast<grpc_end2end_test_fixture*>(ts);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_endpoint_pair* sfd = static_cast<grpc_endpoint_pair*>(f->fixture_data);
  grpc_endpoint_add_to_pollset(&exec_ctx, sfd->server, grpc_cq_pollset(f->cq));
  grpc_server_setup_transport(&exec_ctx, f->server, transport, nullptr,
                              grpc_server_get_channel_args(f->server));
  grpc_exec_ctx_finish(&exec_ctx);
}

typedef struct {
  grpc_end2end_test_fixture* f;
  grpc_channel_args* client_args;
} sp_client_setup;

static void client_setup_transport(grpc_exec_ctx* exec_ctx, void* ts,
                                   grpc_transport* transport) {
  sp_client_setup* cs = static_cast<sp_client_setup*>(ts);

  cs->f->client =
      grpc_channel_create(exec_ctx, "socketpair-target", cs->client_args,
                          GRPC_CLIENT_DIRECT_CHANNEL, transport);
}

static grpc_end2end_test_fixture chttp2_create_fixture_socketpair(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  grpc_endpoint_pair* sfd =
      static_cast<grpc_endpoint_pair*>(gpr_malloc(sizeof(grpc_endpoint_pair)));

  grpc_end2end_test_fixture f;
  memset(&f, 0, sizeof(f));
  f.fixture_data = sfd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);

  *sfd = grpc_iomgr_create_endpoint_pair("fixture", nullptr);

  return f;
}

static void chttp2_init_client_socketpair(grpc_end2end_test_fixture* f,
                                          grpc_channel_args* client_args) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_endpoint_pair* sfd = static_cast<grpc_endpoint_pair*>(f->fixture_data);
  grpc_transport* transport;
  sp_client_setup cs;
  cs.client_args = client_args;
  cs.f = f;
  transport =
      grpc_create_chttp2_transport(&exec_ctx, client_args, sfd->client, 1);
  client_setup_transport(&exec_ctx, &cs, transport);
  GPR_ASSERT(f->client);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void chttp2_init_server_socketpair(grpc_end2end_test_fixture* f,
                                          grpc_channel_args* server_args) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_endpoint_pair* sfd = static_cast<grpc_endpoint_pair*>(f->fixture_data);
  grpc_transport* transport;
  GPR_ASSERT(!f->server);
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_start(f->server);
  transport =
      grpc_create_chttp2_transport(&exec_ctx, server_args, sfd->server, 0);
  server_setup_transport(f, transport);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, nullptr);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void chttp2_tear_down_socketpair(grpc_end2end_test_fixture* f) {
  gpr_free(f->fixture_data);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/socketpair", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
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
