/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct inproc_fixture_data {
  bool dummy;  // reserved for future expansion. Struct can't be empty
} inproc_fixture_data;

static grpc_end2end_test_fixture inproc_create_fixture(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  inproc_fixture_data* ffd = static_cast<inproc_fixture_data*>(
      gpr_malloc(sizeof(inproc_fixture_data)));
  memset(&f, 0, sizeof(f));

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);

  return f;
}

void inproc_init_client(grpc_end2end_test_fixture* f,
                        grpc_channel_args* client_args) {
  f->client = grpc_inproc_channel_create(f->server, client_args, nullptr);
  GPR_ASSERT(f->client);
}

void inproc_init_server(grpc_end2end_test_fixture* f,
                        grpc_channel_args* server_args) {
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_start(f->server);
}

void inproc_tear_down(grpc_end2end_test_fixture* f) {
  inproc_fixture_data* ffd = static_cast<inproc_fixture_data*>(f->fixture_data);
  gpr_free(ffd);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"inproc", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
     inproc_create_fixture, inproc_init_client, inproc_init_server,
     inproc_tear_down},
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
