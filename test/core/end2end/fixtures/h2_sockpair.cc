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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

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
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

/* chttp2 transport that is immediately available (used for testing
   connected_channel without a client_channel */

struct custom_fixture_data {
  grpc_endpoint_pair ep;
  grpc_resource_quota* resource_quota;
};

static void server_setup_transport(void* ts, grpc_transport* transport) {
  grpc_end2end_test_fixture* f = static_cast<grpc_end2end_test_fixture*>(ts);
  grpc_core::ExecCtx exec_ctx;
  custom_fixture_data* fixture_data =
      static_cast<custom_fixture_data*>(f->fixture_data);
  grpc_endpoint_add_to_pollset(fixture_data->ep.server, grpc_cq_pollset(f->cq));
  grpc_error_handle error = f->server->core_server->SetupTransport(
      transport, nullptr, f->server->core_server->channel_args(), nullptr);
  if (error == GRPC_ERROR_NONE) {
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
  } else {
    GRPC_ERROR_UNREF(error);
    grpc_transport_destroy(transport);
  }
}

typedef struct {
  grpc_end2end_test_fixture* f;
  grpc_channel_args* client_args;
} sp_client_setup;

static void client_setup_transport(void* ts, grpc_transport* transport) {
  sp_client_setup* cs = static_cast<sp_client_setup*>(ts);

  grpc_arg authority_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>("test-authority"));
  grpc_channel_args* args =
      grpc_channel_args_copy_and_add(cs->client_args, &authority_arg, 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  cs->f->client =
      grpc_channel_create("socketpair-target", args, GRPC_CLIENT_DIRECT_CHANNEL,
                          transport, nullptr, 0, &error);
  grpc_channel_args_destroy(args);
  if (cs->f->client != nullptr) {
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
  } else {
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    cs->f->client =
        grpc_lame_client_channel_create(nullptr, status, "lame channel");
    grpc_transport_destroy(transport);
  }
}

static grpc_end2end_test_fixture chttp2_create_fixture_socketpair(
    grpc_channel_args* client_args, grpc_channel_args* /*server_args*/) {
  custom_fixture_data* fixture_data = static_cast<custom_fixture_data*>(
      gpr_malloc(sizeof(custom_fixture_data)));
  grpc_end2end_test_fixture f;
  memset(&f, 0, sizeof(f));
  f.fixture_data = fixture_data;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
  fixture_data->resource_quota =
      grpc_resource_quota_from_channel_args(client_args, true);
  fixture_data->ep = grpc_iomgr_create_endpoint_pair("fixture", nullptr);
  return f;
}

static void chttp2_init_client_socketpair(grpc_end2end_test_fixture* f,
                                          grpc_channel_args* client_args) {
  grpc_core::ExecCtx exec_ctx;
  auto* fixture_data = static_cast<custom_fixture_data*>(f->fixture_data);
  grpc_transport* transport;
  sp_client_setup cs;
  cs.client_args = client_args;
  cs.f = f;
  transport = grpc_create_chttp2_transport(
      client_args, fixture_data->ep.client, true,
      grpc_resource_user_create(fixture_data->resource_quota,
                                "client_transport"));
  client_setup_transport(&cs, transport);
  GPR_ASSERT(f->client);
}

static void chttp2_init_server_socketpair(grpc_end2end_test_fixture* f,
                                          grpc_channel_args* server_args) {
  grpc_core::ExecCtx exec_ctx;
  auto* fixture_data = static_cast<custom_fixture_data*>(f->fixture_data);
  grpc_transport* transport;
  GPR_ASSERT(!f->server);
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_start(f->server);
  transport = grpc_create_chttp2_transport(
      server_args, fixture_data->ep.server, false,
      grpc_resource_user_create(fixture_data->resource_quota,
                                "server_transport"));
  server_setup_transport(f, transport);
}

static void chttp2_tear_down_socketpair(grpc_end2end_test_fixture* f) {
  grpc_core::ExecCtx exec_ctx;
  auto* fixture_data = static_cast<custom_fixture_data*>(f->fixture_data);
  grpc_resource_quota_unref(fixture_data->resource_quota);
  gpr_free(f->fixture_data);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/socketpair", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
     chttp2_create_fixture_socketpair, chttp2_init_client_socketpair,
     chttp2_init_server_socketpair, chttp2_tear_down_socketpair},
};

int main(int argc, char** argv) {
  size_t i;

  grpc::testing::TestEnvironment env(argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
