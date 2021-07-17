//
//
// Copyright 2020 gRPC authors.
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

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace {

struct Chttp2InsecureFullstackFixtureData {
  std::string localaddr;
};

grpc_end2end_test_fixture Chttp2CreateFixtureInsecureFullstack(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  Chttp2InsecureFullstackFixtureData* ffd =
      new Chttp2InsecureFullstackFixtureData();
  memset(&f, 0, sizeof(f));
  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);

  return f;
}

void Chttp2InitClientInsecureFullstack(grpc_end2end_test_fixture* f,
                                       grpc_channel_args* client_args) {
  Chttp2InsecureFullstackFixtureData* ffd =
      static_cast<Chttp2InsecureFullstackFixtureData*>(f->fixture_data);
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  f->client = grpc_secure_channel_create(creds, ffd->localaddr.c_str(),
                                         client_args, nullptr);
  grpc_channel_credentials_release(creds);
  GPR_ASSERT(f->client);
}

void ProcessAuthFailure(void* state, grpc_auth_context* /*ctx*/,
                        const grpc_metadata* /*md*/, size_t /*md_count*/,
                        grpc_process_auth_metadata_done_cb cb,
                        void* user_data) {
  GPR_ASSERT(state == nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

void Chttp2InitServerInsecureFullstack(grpc_end2end_test_fixture* f,
                                       grpc_channel_args* server_args) {
  Chttp2InsecureFullstackFixtureData* ffd =
      static_cast<Chttp2InsecureFullstackFixtureData*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_credentials* server_creds =
      grpc_insecure_server_credentials_create();
  if (grpc_channel_args_find(server_args, FAIL_AUTH_CHECK_SERVER_ARG_NAME) !=
      nullptr) {
    grpc_auth_metadata_processor processor = {ProcessAuthFailure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(server_creds,
                                                        processor);
  }
  GPR_ASSERT(grpc_server_add_secure_http2_port(
      f->server, ffd->localaddr.c_str(), server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void Chttp2TearDownInsecureFullstack(grpc_end2end_test_fixture* f) {
  Chttp2InsecureFullstackFixtureData* ffd =
      static_cast<Chttp2InsecureFullstackFixtureData*>(f->fixture_data);
  delete ffd;
}

/* All test configurations */
grpc_end2end_test_config configs[] = {
    {"chttp2/insecure_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE,
     nullptr, Chttp2CreateFixtureInsecureFullstack,
     Chttp2InitClientInsecureFullstack, Chttp2InitServerInsecureFullstack,
     Chttp2TearDownInsecureFullstack},
};

}  // namespace

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
