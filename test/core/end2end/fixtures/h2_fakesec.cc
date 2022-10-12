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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

struct fullstack_secure_fixture_data {
  std::string localaddr;
};

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    const grpc_channel_args* /*client_args*/,
    const grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));
  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);

  return f;
}

static void process_auth_failure(void* state, grpc_auth_context* /*ctx*/,
                                 const grpc_metadata* /*md*/,
                                 size_t /*md_count*/,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void* user_data) {
  GPR_ASSERT(state == nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

static void chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture* f, const grpc_channel_args* client_args,
    grpc_channel_credentials* creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  f->client = grpc_channel_create(ffd->localaddr.c_str(), creds, client_args);
  GPR_ASSERT(f->client != nullptr);
  grpc_channel_credentials_release(creds);
}

static void chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture* f, const grpc_channel_args* server_args,
    grpc_server_credentials* server_creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  GPR_ASSERT(grpc_server_add_http2_port(f->server, ffd->localaddr.c_str(),
                                        server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  delete ffd;
}

static void chttp2_init_client_fake_secure_fullstack(
    grpc_end2end_test_fixture* f, const grpc_channel_args* client_args) {
  grpc_channel_credentials* fake_ts_creds =
      grpc_fake_transport_security_credentials_create();
  chttp2_init_client_secure_fullstack(f, client_args, fake_ts_creds);
}

static int fail_server_auth_check(const grpc_channel_args* server_args) {
  size_t i;
  if (server_args == nullptr) return 0;
  for (i = 0; i < server_args->num_args; i++) {
    if (strcmp(server_args->args[i].key, FAIL_AUTH_CHECK_SERVER_ARG_NAME) ==
        0) {
      return 1;
    }
  }
  return 0;
}

static void chttp2_init_server_fake_secure_fullstack(
    grpc_end2end_test_fixture* f, const grpc_channel_args* server_args) {
  grpc_server_credentials* fake_ts_creds =
      grpc_fake_transport_security_server_credentials_create();
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(fake_ts_creds,
                                                        processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, fake_ts_creds);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/fake_secure_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE,
     nullptr, chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_fake_secure_fullstack,
     chttp2_init_server_fake_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
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
