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

#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <utility>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

struct fullstack_fixture_data {
  std::string localaddr;
};

static int unique = 1;

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack_base(
    std::string addr) {
  fullstack_fixture_data* ffd = new fullstack_fixture_data;
  ffd->localaddr = std::move(addr);

  grpc_end2end_test_fixture f;
  memset(&f, 0, sizeof(f));
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);

  return f;
}

static grpc_end2end_test_fixture
chttp2_create_fixture_fullstack_abstract_namespace(
    const grpc_channel_args* /*client_args*/,
    const grpc_channel_args* /*server_args*/) {
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  const std::string localaddr = absl::StrFormat(
      "unix-abstract:grpc_fullstack_test.%d.%" PRId64 ".%" PRId32 ".%d",
      getpid(), now.tv_sec, now.tv_nsec, unique++);
  return chttp2_create_fixture_fullstack_base(localaddr);
}

void chttp2_init_client_fullstack(grpc_end2end_test_fixture* f,
                                  const grpc_channel_args* client_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  f->client = grpc_channel_create(ffd->localaddr.c_str(), creds, client_args);
  grpc_channel_credentials_release(creds);
}

void chttp2_init_server_fullstack(grpc_end2end_test_fixture* f,
                                  const grpc_channel_args* server_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_credentials* server_creds =
      grpc_insecure_server_credentials_create();
  GPR_ASSERT(grpc_server_add_http2_port(f->server, ffd->localaddr.c_str(),
                                        server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  delete ffd;
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack_uds_abstract_namespace",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     nullptr, chttp2_create_fixture_fullstack_abstract_namespace,
     chttp2_init_client_fullstack, chttp2_init_server_fullstack,
     chttp2_tear_down_fullstack},
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
