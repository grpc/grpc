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

#include "test/core/end2end/fixtures/local_util.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

grpc_end2end_test_fixture grpc_end2end_local_chttp2_create_fixture_fullstack() {
  grpc_end2end_test_fixture f;
  grpc_end2end_local_fullstack_fixture_data* ffd =
      new grpc_end2end_local_fullstack_fixture_data();
  memset(&f, 0, sizeof(f));
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
  return f;
}

void grpc_end2end_local_chttp2_init_client_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args,
    grpc_local_connect_type type) {
  grpc_channel_credentials* creds = grpc_local_credentials_create(type);
  grpc_end2end_local_fullstack_fixture_data* ffd =
      static_cast<grpc_end2end_local_fullstack_fixture_data*>(f->fixture_data);
  f->client = grpc_secure_channel_create(creds, ffd->localaddr.c_str(),
                                         client_args, nullptr);
  GPR_ASSERT(f->client != nullptr);
  grpc_channel_credentials_release(creds);
}

/*
 * Check if server should fail auth check. If it is true, a different metadata
 * processor will be installed that always fails in processing client's
 * metadata.
 */
static bool fail_server_auth_check(grpc_channel_args* server_args) {
  size_t i;
  if (server_args == nullptr) return false;
  for (i = 0; i < server_args->num_args; i++) {
    if (strcmp(server_args->args[i].key, FAIL_AUTH_CHECK_SERVER_ARG_NAME) ==
        0) {
      return true;
    }
  }
  return false;
}

static void process_auth_failure(void* state, grpc_auth_context* /*ctx*/,
                                 const grpc_metadata* /*md*/,
                                 size_t /*md_count*/,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void* user_data) {
  GPR_ASSERT(state == nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

void grpc_end2end_local_chttp2_init_server_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args,
    grpc_local_connect_type type) {
  grpc_server_credentials* creds = grpc_local_server_credentials_create(type);
  grpc_end2end_local_fullstack_fixture_data* ffd =
      static_cast<grpc_end2end_local_fullstack_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(creds, processor);
  }
  GPR_ASSERT(grpc_server_add_secure_http2_port(f->server,
                                               ffd->localaddr.c_str(), creds));
  grpc_server_credentials_release(creds);
  grpc_server_start(f->server);
}

void grpc_end2end_local_chttp2_tear_down_fullstack(
    grpc_end2end_test_fixture* f) {
  grpc_end2end_local_fullstack_fixture_data* ffd =
      static_cast<grpc_end2end_local_fullstack_fixture_data*>(f->fixture_data);
  delete ffd;
}
