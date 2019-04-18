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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef struct fullstack_secure_fixture_data {
  grpc_end2end_proxy* proxy;
} fullstack_secure_fixture_data;

static grpc_server* create_proxy_server(const char* port,
                                        grpc_channel_args* server_args) {
  grpc_server* s = grpc_server_create(server_args, nullptr);
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
      nullptr, &pem_cert_key_pair, 1, 0, nullptr);
  GPR_ASSERT(grpc_server_add_secure_http2_port(s, port, ssl_creds));
  grpc_server_credentials_release(ssl_creds);
  return s;
}

static grpc_channel* create_proxy_client(const char* target,
                                         grpc_channel_args* client_args) {
  grpc_channel* channel;
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  channel =
      grpc_secure_channel_create(ssl_creds, target, new_client_args, nullptr);
  grpc_channel_credentials_release(ssl_creds);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(new_client_args);
  }
  return channel;
}

static const grpc_end2end_proxy_def proxy_def = {create_proxy_server,
                                                 create_proxy_client};

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(
          gpr_malloc(sizeof(fullstack_secure_fixture_data)));
  memset(&f, 0, sizeof(f));

  ffd->proxy = grpc_end2end_proxy_create(&proxy_def, client_args, server_args);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);

  return f;
}

static void process_auth_failure(void* state, grpc_auth_context* ctx,
                                 const grpc_metadata* md, size_t md_count,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void* user_data) {
  GPR_ASSERT(state == nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

static void chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args,
    grpc_channel_credentials* creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  f->client = grpc_secure_channel_create(
      creds, grpc_end2end_proxy_get_client_target(ffd->proxy), client_args,
      nullptr);
  GPR_ASSERT(f->client != nullptr);
  grpc_channel_credentials_release(creds);
}

static void chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args,
    grpc_server_credentials* server_creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  GPR_ASSERT(grpc_server_add_secure_http2_port(
      f->server, grpc_end2end_proxy_get_server_port(ffd->proxy), server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  grpc_end2end_proxy_destroy(ffd->proxy);
  gpr_free(ffd);
}

static void chttp2_init_client_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(new_client_args);
  }
}

static int fail_server_auth_check(grpc_channel_args* server_args) {
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

static void chttp2_init_server_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args) {
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
      nullptr, &pem_cert_key_pair, 1, 0, nullptr);
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_REQUEST_PROXYING |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_simple_ssl_secure_fullstack,
     chttp2_init_server_simple_ssl_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
};

int main(int argc, char** argv) {
  size_t i;
  FILE* roots_file;
  size_t roots_size = strlen(test_root_cert);
  char* roots_filename;

  grpc::testing::TestEnvironment env(argc, argv);
  grpc_end2end_tests_pre_init();

  /* Set the SSL roots env var. */
  roots_file = gpr_tmpfile("chttp2_simple_ssl_fullstack_test", &roots_filename);
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
