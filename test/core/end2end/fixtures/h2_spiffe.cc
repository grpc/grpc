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
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

typedef grpc_core::InlinedVector<grpc_core::Thread, 1> ThreadList;

struct fullstack_secure_fixture_data {
  ~fullstack_secure_fixture_data() {
    for (size_t ind = 0; ind < thd_list.size(); ind++) {
      thd_list[ind].Join();
    }
  }
  grpc_core::UniquePtr<char> localaddr;
  ThreadList thd_list;
};

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));
  grpc_core::JoinHostPort(&ffd->localaddr, "localhost", port);
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
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
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args,
    grpc_channel_credentials* creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  f->client = grpc_secure_channel_create(creds, ffd->localaddr.get(),
                                         client_args, nullptr);
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
  GPR_ASSERT(grpc_server_add_secure_http2_port(f->server, ffd->localaddr.get(),
                                               server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  delete ffd;
}

// Application-provided callback for server authorization check.
static void server_authz_check_cb(void* user_data) {
  grpc_tls_server_authorization_check_arg* check_arg =
      static_cast<grpc_tls_server_authorization_check_arg*>(user_data);
  GPR_ASSERT(check_arg != nullptr);
  // result = 1 indicates the server authorization check passes.
  // Normally, the application code should resort to mapping information
  // between server identity and target name to derive the result.
  // For this test, we directly return 1 for simplicity.
  check_arg->success = 1;
  check_arg->status = GRPC_STATUS_OK;
  check_arg->cb(check_arg);
}

// Asynchronous implementation of schedule field in
// grpc_server_authorization_check_config.
static int server_authz_check_async(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(config_user_data);
  ffd->thd_list.push_back(
      grpc_core::Thread("h2_spiffe_test", &server_authz_check_cb, arg));
  ffd->thd_list[ffd->thd_list.size() - 1].Start();
  return 1;
}

// Synchronous implementation of schedule field in
// grpc_tls_credential_reload_config instance that is a part of client-side
// grpc_tls_credentials_options instance.
static int client_cred_reload_sync(void* /*config_user_data*/,
                                   grpc_tls_credential_reload_arg* arg) {
  if (!arg->key_materials_config->pem_key_cert_pair_list().empty()) {
    arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
    return 0;
  }
  grpc_ssl_pem_key_cert_pair** key_cert_pair =
      static_cast<grpc_ssl_pem_key_cert_pair**>(
          gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair*)));
  key_cert_pair[0] = static_cast<grpc_ssl_pem_key_cert_pair*>(
      gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  key_cert_pair[0]->private_key = gpr_strdup(test_server1_key);
  key_cert_pair[0]->cert_chain = gpr_strdup(test_server1_cert);
  if (arg->key_materials_config->pem_key_cert_pair_list().empty()) {
    grpc_tls_key_materials_config_set_key_materials(
        arg->key_materials_config, gpr_strdup(test_root_cert),
        (const grpc_ssl_pem_key_cert_pair**)key_cert_pair, 1);
  }
  // new credential has been reloaded.
  arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
  return 0;
}

// Synchronous implementation of schedule field in
// grpc_tls_credential_reload_config instance that is a part of server-side
// grpc_tls_credentials_options instance.
static int server_cred_reload_sync(void* /*config_user_data*/,
                                   grpc_tls_credential_reload_arg* arg) {
  if (!arg->key_materials_config->pem_key_cert_pair_list().empty()) {
    arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
    return 0;
  }
  grpc_ssl_pem_key_cert_pair** key_cert_pair =
      static_cast<grpc_ssl_pem_key_cert_pair**>(
          gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair*)));
  key_cert_pair[0] = static_cast<grpc_ssl_pem_key_cert_pair*>(
      gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  key_cert_pair[0]->private_key = gpr_strdup(test_server1_key);
  key_cert_pair[0]->cert_chain = gpr_strdup(test_server1_cert);
  GPR_ASSERT(arg != nullptr);
  GPR_ASSERT(arg->key_materials_config != nullptr);
  GPR_ASSERT(arg->key_materials_config->pem_key_cert_pair_list().data() !=
             nullptr);
  if (arg->key_materials_config->pem_key_cert_pair_list().empty()) {
    grpc_tls_key_materials_config_set_key_materials(
        arg->key_materials_config, gpr_strdup(test_root_cert),
        (const grpc_ssl_pem_key_cert_pair**)key_cert_pair, 1);
  }
  // new credential has been reloaded.
  arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
  return 0;
}

// Create a SPIFFE channel credential.
static grpc_channel_credentials* create_spiffe_channel_credentials(
    fullstack_secure_fixture_data* ffd) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  /* Set credential reload config. */
  grpc_tls_credential_reload_config* reload_config =
      grpc_tls_credential_reload_config_create(nullptr, client_cred_reload_sync,
                                               nullptr, nullptr);
  grpc_tls_credentials_options_set_credential_reload_config(options,
                                                            reload_config);
  /* Set server authorization check config. */
  grpc_tls_server_authorization_check_config* check_config =
      grpc_tls_server_authorization_check_config_create(
          ffd, server_authz_check_async, nullptr, nullptr);
  grpc_tls_credentials_options_set_server_authorization_check_config(
      options, check_config);
  /* Create SPIFFE channel credentials. */
  grpc_channel_credentials* creds = grpc_tls_spiffe_credentials_create(options);
  return creds;
}

// Create a SPIFFE server credential.
static grpc_server_credentials* create_spiffe_server_credentials() {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  /* Set credential reload config. */
  grpc_tls_credential_reload_config* reload_config =
      grpc_tls_credential_reload_config_create(nullptr, server_cred_reload_sync,
                                               nullptr, nullptr);
  grpc_tls_credentials_options_set_credential_reload_config(options,
                                                            reload_config);
  /* Set client certificate request type. */
  grpc_tls_credentials_options_set_cert_request_type(
      options, GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  grpc_server_credentials* creds =
      grpc_tls_spiffe_server_credentials_create(options);
  return creds;
}

static void chttp2_init_client(grpc_end2end_test_fixture* f,
                               grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds = create_spiffe_channel_credentials(
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data));
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

static void chttp2_init_server(grpc_end2end_test_fixture* f,
                               grpc_channel_args* server_args) {
  grpc_server_credentials* ssl_creds = create_spiffe_server_credentials();
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

static grpc_end2end_test_config configs[] = {
    /* client sync reload async authz + server sync reload. */
    {"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack,
     chttp2_init_client, chttp2_init_server, chttp2_tear_down_secure_fullstack},
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
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, roots_filename);
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
