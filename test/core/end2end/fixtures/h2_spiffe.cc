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

#include <stdio.h>
#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <grpc/support/string_util.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define TOTAL_NUM_THREADS 100
static grpc_core::Thread threads[TOTAL_NUM_THREADS];
static size_t num_threads = 0;

typedef struct fullstack_secure_fixture_data {
  char* localaddr;
} fullstack_secure_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(
          gpr_malloc(sizeof(fullstack_secure_fixture_data)));
  memset(&f, 0, sizeof(f));

  gpr_join_host_port(&ffd->localaddr, "localhost", port);
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
  f->client =
      grpc_secure_channel_create(creds, ffd->localaddr, client_args, nullptr);
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
  GPR_ASSERT(grpc_server_add_secure_http2_port(f->server, ffd->localaddr,
                                               server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  for (size_t ind = 0; ind < num_threads; ind++) {
    threads[ind].Join();
  }
  num_threads = 0;
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
}

static void server_authz_check_cb(void* user_data) {
  grpc_tls_server_authorization_check_arg* check_arg =
      static_cast<grpc_tls_server_authorization_check_arg*>(user_data);
  GPR_ASSERT(check_arg != nullptr);
  check_arg->result = 1;
  check_arg->status = GRPC_STATUS_OK;
  check_arg->cb(check_arg);
}

static int server_authz_check_sync(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg) {
  arg->result = 1;
  arg->status = GRPC_STATUS_OK;
  return 0;
}

static int server_authz_check_async(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg) {
  threads[num_threads] =
      grpc_core::Thread("h2_spiffe_test", &server_authz_check_cb, arg);
  threads[num_threads].Start();
  num_threads++;
  return 1;
}

static int client_cred_reload_sync(void* config_user_data,
                                   grpc_tls_credential_reload_arg* arg) {
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_signed_client_key,
                                                  test_signed_client_cert};
  if (!arg->key_materials_config->num_key_cert_pairs()) {
    grpc_tls_key_materials_config_set_key_materials(
        arg->key_materials_config, &pem_cert_key_pair, nullptr, 1);
  }
  return 0;
}

static int server_cred_reload_sync(void* config_user_data,
                                   grpc_tls_credential_reload_arg* arg) {
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  if (!arg->key_materials_config->num_key_cert_pairs()) {
    grpc_tls_key_materials_config_set_key_materials(
        arg->key_materials_config, &pem_cert_key_pair, nullptr, 1);
  }
  return 0;
}

static void client_cred_reload_done_cb(void* user_data) {
  grpc_tls_credential_reload_arg* reload_arg =
      static_cast<grpc_tls_credential_reload_arg*>(user_data);
  GPR_ASSERT(reload_arg != nullptr);
  client_cred_reload_sync(nullptr, reload_arg);
  reload_arg->status = GRPC_STATUS_OK;
  reload_arg->cb(reload_arg);
}

static void server_cred_reload_done_cb(void* user_data) {
  grpc_tls_credential_reload_arg* reload_arg =
      static_cast<grpc_tls_credential_reload_arg*>(user_data);
  GPR_ASSERT(reload_arg != nullptr);
  server_cred_reload_sync(nullptr, reload_arg);
  reload_arg->status = GRPC_STATUS_OK;
  reload_arg->cb(reload_arg);
}

static int client_cred_reload_async(void* config_user_data,
                                    grpc_tls_credential_reload_arg* arg) {
  threads[num_threads] =
      grpc_core::Thread("h2_spiffe_test", &client_cred_reload_done_cb, arg);
  threads[num_threads].Start();
  num_threads++;
  return 1;
}

static int server_cred_reload_async(void* config_user_data,
                                    grpc_tls_credential_reload_arg* arg) {
  threads[num_threads] =
      grpc_core::Thread("h2_spiffe_test", &server_cred_reload_done_cb, arg);
  threads[num_threads].Start();
  num_threads++;
  return 1;
}

static grpc_channel_credentials* create_spiffe_channel_credentials(
    bool reload_sync, bool authz_check_sync) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  /* Set credential reload config. */
  grpc_tls_credential_reload_config* reload_config =
      reload_sync ? grpc_tls_credential_reload_config_create(
                        nullptr, client_cred_reload_sync, nullptr, nullptr)
                  : grpc_tls_credential_reload_config_create(
                        nullptr, client_cred_reload_async, nullptr, nullptr);
  grpc_tls_credentials_options_set_credential_reload_config(options,
                                                            reload_config);
  /* Set server authorization check config. */
  grpc_tls_server_authorization_check_config* check_config =
      authz_check_sync
          ? grpc_tls_server_authorization_check_config_create(
                nullptr, server_authz_check_sync, nullptr, nullptr)
          : grpc_tls_server_authorization_check_config_create(
                nullptr, server_authz_check_async, nullptr, nullptr);
  grpc_tls_credentials_options_set_server_authorization_check_config(
      options, check_config);
  /* Create SPIFFE channel credentials. */
  grpc_channel_credentials* creds = grpc_tls_spiffe_credentials_create(options);

  return creds;
}

static grpc_server_credentials* create_spiffe_server_credentials(
    bool reload_sync) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  /* Set credential reload config. */
  grpc_tls_credential_reload_config* reload_config =
      reload_sync ? grpc_tls_credential_reload_config_create(
                        nullptr, server_cred_reload_sync, nullptr, nullptr)
                  : grpc_tls_credential_reload_config_create(
                        nullptr, server_cred_reload_async, nullptr, nullptr);
  grpc_tls_credentials_options_set_credential_reload_config(options,
                                                            reload_config);
  grpc_server_credentials* creds =
      grpc_tls_spiffe_server_credentials_create(options);
  return creds;
}
/*
static void chttp2_init_client_spiffe_sync_reload_sync_authz(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds =
      create_spiffe_channel_credentials(true, true);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);
  grpc_channel_args_destroy(new_client_args);
}

static void chttp2_init_client_spiffe_async_reload_sync_authz(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds =
      create_spiffe_channel_credentials(false, true);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);
  grpc_channel_args_destroy(new_client_args);
}

static void chttp2_init_client_spiffe_sync_reload_async_authz(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds =
      create_spiffe_channel_credentials(true, false);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);
  grpc_channel_args_destroy(new_client_args);
}
*/

static void chttp2_init_client_spiffe_async_reload_async_authz(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds =
      create_spiffe_channel_credentials(false, false);
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);
  grpc_channel_args_destroy(new_client_args);
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

/*
static void chttp2_init_server_spiffe_sync_reload(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args) {
  grpc_server_credentials* ssl_creds = create_spiffe_server_credentials(true);
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}
*/

static void chttp2_init_server_spiffe_async_reload(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args) {
  grpc_server_credentials* ssl_creds = create_spiffe_server_credentials(false);
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}
/* All test configurations */

static grpc_end2end_test_config configs[] = {
    /* client sync reload sync authz + server sync reload. */
    /*{"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_sync_reload_sync_authz,
     chttp2_init_server_spiffe_sync_reload,
     chttp2_tear_down_secure_fullstack},*/
    /* client async reload sync authz + server sync reload. */
    /*{"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_async_reload_sync_authz,
     chttp2_init_server_spiffe_sync_reload,
     chttp2_tear_down_secure_fullstack},*/
    /* client sync reload async authz + server sync reload. */
    /*{"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_sync_reload_async_authz,
     chttp2_init_server_spiffe_sync_reload,
     chttp2_tear_down_secure_fullstack},*/
    /* client async reload async authz + server sync reload. */
    /*{"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_async_reload_async_authz,
     chttp2_init_server_spiffe_sync_reload,
     chttp2_tear_down_secure_fullstack},*/
    /* client sync reload sync authz + server async reload. */
    /********************************************************/
    /*{"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_sync_reload_sync_authz,
     chttp2_init_server_spiffe_async_reload,
     chttp2_tear_down_secure_fullstack},*/
    /********************************************************/
    /* client async reload sync authz + server async reload. */
    /*{"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_async_reload_sync_authz,
     chttp2_init_server_spiffe_async_reload,
     chttp2_tear_down_secure_fullstack},*/
    /* client sync reload async authz + server async reload. */
    /*{"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_sync_reload_async_authz,
     chttp2_init_server_spiffe_async_reload,
     chttp2_tear_down_secure_fullstack},*/
    /* client async reload async authz + server async reload. */
    {"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client_spiffe_async_reload_async_authz,
     chttp2_init_server_spiffe_async_reload, chttp2_tear_down_secure_fullstack},
};

int main(int argc, char** argv) {
  FILE* roots_file;
  size_t roots_size = strlen(test_root_cert);
  char* roots_filename;
  grpc_test_init(argc, argv);
  grpc_end2end_tests_pre_init();
  /* Set the SSL roots env var. */
  roots_file = gpr_tmpfile("chttp2_simple_ssl_fullstack_test", &roots_filename);
  GPR_ASSERT(roots_filename != nullptr);
  GPR_ASSERT(roots_file != nullptr);
  GPR_ASSERT(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  gpr_setenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR, roots_filename);
  grpc_init();
  for (size_t ind = 0; ind < sizeof(configs) / sizeof(*configs); ind++) {
    grpc_end2end_tests(argc, argv, configs[ind]);
  }
  grpc_shutdown();
  /* Cleanup. */
  remove(roots_filename);
  gpr_free(roots_filename);
  return 0;
}
