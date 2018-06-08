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

#include "test/core/end2end/end2end_tests.h"

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
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static void chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args) {
  grpc_core::ExecCtx exec_ctx;
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(nullptr, nullptr, nullptr);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  set_http_proxy(grpc_end2end_http_proxy_get_proxy_name(ffd->proxy),
                 client_args, true);
  f->client = grpc_secure_channel_create(ssl_creds, ffd->server_addr,
                                         new_client_args, nullptr);
  GPR_ASSERT(f->client);
  grpc_channel_credentials_release(ssl_creds);
  grpc_channel_args_destroy(new_client_args);
}

static void chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
      nullptr, &pem_cert_key_pair, 1, 0, nullptr);
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  GPR_ASSERT(grpc_server_add_secure_http2_port(f->server, ffd->server_addr,
                                               ssl_creds));
  grpc_server_credentials_release(ssl_creds);
  grpc_server_start(f->server);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_fullstack,
     chttp2_init_client_secure_fullstack, chttp2_init_server_secure_fullstack,
     chttp2_tear_down_fullstack},
};

int main(int argc, char** argv) {
  size_t i;

  FILE* roots_file;
  size_t roots_size = strlen(test_root_cert);
  char* roots_filename;

  grpc_test_init(argc, argv);
  grpc_end2end_tests_pre_init();

  /* Set the SSL roots env var. */
  roots_file = gpr_tmpfile("chttp2_https_proxy_test", &roots_filename);
  GPR_ASSERT(roots_filename != nullptr);
  GPR_ASSERT(roots_file != nullptr);
  GPR_ASSERT(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  gpr_setenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR, roots_filename);

  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  /* Cleanup. */
  remove(roots_filename);
  gpr_free(roots_filename);

  return 0;
}
