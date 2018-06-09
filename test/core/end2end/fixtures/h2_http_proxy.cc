/*
 *
 * Copyright 2018 gRPC authors.
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

/* This fixture runs the end to end tests with an http proxy in between. */

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static void chttp2_init_client_fullstack(grpc_end2end_test_fixture* f,
                                         grpc_channel_args* client_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  set_http_proxy(grpc_end2end_http_proxy_get_proxy_name(ffd->proxy),
                 client_args, false);
  f->client =
      grpc_insecure_channel_create(ffd->server_addr, client_args, nullptr);
  GPR_ASSERT(f->client);
}

static void chttp2_init_server_fullstack(grpc_end2end_test_fixture* f,
                                         grpc_channel_args* server_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  GPR_ASSERT(grpc_server_add_insecure_http2_port(f->server, ffd->server_addr));
  grpc_server_start(f->server);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
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
