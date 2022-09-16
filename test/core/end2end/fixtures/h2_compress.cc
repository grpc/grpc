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

#include <string>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/compression/args_utils.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

struct fullstack_compression_fixture_data {
  ~fullstack_compression_fixture_data() {
    grpc_channel_args_destroy(client_args_compression);
    grpc_channel_args_destroy(server_args_compression);
  }
  std::string localaddr;
  const grpc_channel_args* client_args_compression = nullptr;
  const grpc_channel_args* server_args_compression = nullptr;
};

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack_compression(
    const grpc_channel_args* /*client_args*/,
    const grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_compression_fixture_data* ffd =
      new fullstack_compression_fixture_data();
  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);

  memset(&f, 0, sizeof(f));
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);

  return f;
}

void chttp2_init_client_fullstack_compression(
    grpc_end2end_test_fixture* f, const grpc_channel_args* client_args) {
  fullstack_compression_fixture_data* ffd =
      static_cast<fullstack_compression_fixture_data*>(f->fixture_data);
  if (ffd->client_args_compression != nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(ffd->client_args_compression);
  }
  ffd->client_args_compression =
      grpc_channel_args_set_channel_default_compression_algorithm(
          client_args, GRPC_COMPRESS_GZIP);
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  f->client = grpc_channel_create(ffd->localaddr.c_str(), creds,
                                  ffd->client_args_compression);
  grpc_channel_credentials_release(creds);
}

void chttp2_init_server_fullstack_compression(
    grpc_end2end_test_fixture* f, const grpc_channel_args* server_args) {
  fullstack_compression_fixture_data* ffd =
      static_cast<fullstack_compression_fixture_data*>(f->fixture_data);
  if (ffd->server_args_compression != nullptr) {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(ffd->server_args_compression);
  }
  ffd->server_args_compression =
      grpc_channel_args_set_channel_default_compression_algorithm(
          server_args, GRPC_COMPRESS_GZIP);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(ffd->server_args_compression, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_credentials* server_creds =
      grpc_insecure_server_credentials_create();
  GPR_ASSERT(grpc_server_add_http2_port(f->server, ffd->localaddr.c_str(),
                                        server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_fullstack_compression(grpc_end2end_test_fixture* f) {
  grpc_core::ExecCtx exec_ctx;
  fullstack_compression_fixture_data* ffd =
      static_cast<fullstack_compression_fixture_data*>(f->fixture_data);
  delete ffd;
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack_compression",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     nullptr, chttp2_create_fixture_fullstack_compression,
     chttp2_init_client_fullstack_compression,
     chttp2_init_server_fullstack_compression,
     chttp2_tear_down_fullstack_compression},
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
