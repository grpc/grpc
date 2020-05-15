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

#include <unistd.h>

#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/host_port.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack_ipv4(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f =
      grpc_end2end_local_chttp2_create_fixture_fullstack();
  int port = grpc_pick_unused_port_or_die();
  static_cast<grpc_end2end_local_fullstack_fixture_data*>(f.fixture_data)
      ->localaddr = grpc_core::JoinHostPort("127.0.0.1", port);
  return f;
}

static void chttp2_init_client_fullstack_ipv4(grpc_end2end_test_fixture* f,
                                              grpc_channel_args* client_args) {
  grpc_end2end_local_chttp2_init_client_fullstack(f, client_args, LOCAL_TCP);
}

static void chttp2_init_server_fullstack_ipv4(grpc_end2end_test_fixture* f,
                                              grpc_channel_args* client_args) {
  grpc_end2end_local_chttp2_init_server_fullstack(f, client_args, LOCAL_TCP);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack_local_ipv4",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS,
     nullptr, chttp2_create_fixture_fullstack_ipv4,
     chttp2_init_client_fullstack_ipv4, chttp2_init_server_fullstack_ipv4,
     grpc_end2end_local_chttp2_tear_down_fullstack}};

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
