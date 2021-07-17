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

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <stdio.h>
#include <string.h>

#include "absl/container/inlined_vector.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

// For normal TLS connections.
#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

typedef absl::InlinedVector<grpc_core::Thread, 1> ThreadList;

struct fullstack_secure_fixture_data {
  ~fullstack_secure_fixture_data() {
    for (size_t ind = 0; ind < thd_list.size(); ind++) {
      thd_list[ind].Join();
    }
    grpc_tls_certificate_provider_release(client_provider);
    grpc_tls_certificate_provider_release(server_provider);
  }
  std::string localaddr;
  grpc_tls_version tls_version;
  ThreadList thd_list;
  grpc_tls_certificate_provider* client_provider = nullptr;
  grpc_tls_certificate_provider* server_provider = nullptr;
};

static grpc_end2end_test_fixture chttp2_create_fixture_static_data(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/,
    grpc_tls_version tls_version) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));
  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);
  ffd->tls_version = tls_version;
  grpc_slice root_slice, cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(CA_CERT_PATH, 1, &root_slice)));
  std::string root_cert =
      std::string(grpc_core::StringViewFromSlice(root_slice));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
  std::string identity_cert =
      std::string(grpc_core::StringViewFromSlice(cert_slice));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
  std::string private_key =
      std::string(grpc_core::StringViewFromSlice(key_slice));
  grpc_tls_identity_pairs* client_pairs = grpc_tls_identity_pairs_create();
  grpc_tls_identity_pairs_add_pair(client_pairs, private_key.c_str(),
                                   identity_cert.c_str());
  ffd->client_provider = grpc_tls_certificate_provider_static_data_create(
      root_cert.c_str(), client_pairs);
  grpc_tls_identity_pairs* server_pairs = grpc_tls_identity_pairs_create();
  grpc_tls_identity_pairs_add_pair(server_pairs, private_key.c_str(),
                                   identity_cert.c_str());
  ffd->server_provider = grpc_tls_certificate_provider_static_data_create(
      root_cert.c_str(), server_pairs);
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
  grpc_slice_unref(root_slice);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  return f;
}

static grpc_end2end_test_fixture chttp2_create_fixture_cert_watcher(
    grpc_channel_args* /*client_args*/, grpc_channel_args* /*server_args*/,
    grpc_tls_version tls_version) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));
  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);
  ffd->tls_version = tls_version;
  ffd->client_provider = grpc_tls_certificate_provider_file_watcher_create(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  ffd->server_provider = grpc_tls_certificate_provider_file_watcher_create(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  f.shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
  return f;
}

static grpc_end2end_test_fixture chttp2_create_fixture_static_data_tls1_2(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  return chttp2_create_fixture_static_data(client_args, server_args,
                                           grpc_tls_version::TLS1_2);
}

static grpc_end2end_test_fixture chttp2_create_fixture_static_data_tls1_3(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  return chttp2_create_fixture_static_data(client_args, server_args,
                                           grpc_tls_version::TLS1_3);
}

static grpc_end2end_test_fixture chttp2_create_fixture_cert_watcher_tls1_2(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  return chttp2_create_fixture_cert_watcher(client_args, server_args,
                                            grpc_tls_version::TLS1_2);
}

static grpc_end2end_test_fixture chttp2_create_fixture_cert_watcher_tls1_3(
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  return chttp2_create_fixture_cert_watcher(client_args, server_args,
                                            grpc_tls_version::TLS1_3);
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
  f->client = grpc_secure_channel_create(creds, ffd->localaddr.c_str(),
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
  GPR_ASSERT(grpc_server_add_secure_http2_port(
      f->server, ffd->localaddr.c_str(), server_creds));
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
      grpc_core::Thread("h2_tls_test", &server_authz_check_cb, arg));
  ffd->thd_list[ffd->thd_list.size() - 1].Start();
  return 1;
}

// Create a TLS channel credential.
static grpc_channel_credentials* create_tls_channel_credentials(
    fullstack_secure_fixture_data* ffd) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  options->set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  options->set_min_tls_version(ffd->tls_version);
  options->set_max_tls_version(ffd->tls_version);
  // Set credential provider.
  grpc_tls_credentials_options_set_certificate_provider(options,
                                                        ffd->client_provider);
  grpc_tls_credentials_options_watch_root_certs(options);
  grpc_tls_credentials_options_watch_identity_key_cert_pairs(options);
  /* Set server authorization check config. */
  grpc_tls_server_authorization_check_config* check_config =
      grpc_tls_server_authorization_check_config_create(
          ffd, server_authz_check_async, nullptr, nullptr);
  grpc_tls_credentials_options_set_server_authorization_check_config(
      options, check_config);
  /* Create TLS channel credentials. */
  grpc_channel_credentials* creds = grpc_tls_credentials_create(options);
  grpc_tls_server_authorization_check_config_release(check_config);
  return creds;
}

// Create a TLS server credential.
static grpc_server_credentials* create_tls_server_credentials(
    fullstack_secure_fixture_data* ffd) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  options->set_min_tls_version(ffd->tls_version);
  options->set_max_tls_version(ffd->tls_version);
  // Set credential provider.
  grpc_tls_credentials_options_set_certificate_provider(options,
                                                        ffd->server_provider);
  grpc_tls_credentials_options_watch_root_certs(options);
  grpc_tls_credentials_options_watch_identity_key_cert_pairs(options);
  /* Set client certificate request type. */
  grpc_tls_credentials_options_set_cert_request_type(
      options, GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  grpc_server_credentials* creds = grpc_tls_server_credentials_create(options);
  return creds;
}

static void chttp2_init_client(grpc_end2end_test_fixture* f,
                               grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds = create_tls_channel_credentials(
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
  grpc_server_credentials* ssl_creds = create_tls_server_credentials(
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data));
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

static grpc_end2end_test_config configs[] = {
    // client: static data provider + async custom verification
    // server: static data provider
    // extra: TLS 1.2
    {"chttp2/simple_ssl_fullstack_tls1_2",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_static_data_tls1_2,
     chttp2_init_client, chttp2_init_server, chttp2_tear_down_secure_fullstack},
    // client: static data provider + async custom verification
    // server: static data provider
    // extra: TLS 1.3
    {"chttp2/simple_ssl_fullstack_tls1_3",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_static_data_tls1_3,
     chttp2_init_client, chttp2_init_server, chttp2_tear_down_secure_fullstack},
    // client: certificate watcher provider + async custom verification
    // server: certificate watcher provider
    // extra: TLS 1.2
    {"chttp2/reloading_from_files_ssl_fullstack_tls1_2",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_cert_watcher_tls1_2,
     chttp2_init_client, chttp2_init_server, chttp2_tear_down_secure_fullstack},
    // client: certificate watcher provider + async custom verification
    // server: certificate watcher provider
    // extra: TLS 1.3
    {"chttp2/reloading_from_files_ssl_fullstack_tls1_3",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     "foo.test.google.fr", chttp2_create_fixture_cert_watcher_tls1_3,
     chttp2_init_client, chttp2_init_server, chttp2_tear_down_secure_fullstack},

};

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_end2end_tests_pre_init();
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, CA_CERT_PATH);
  grpc_init();
  for (size_t ind = 0; ind < sizeof(configs) / sizeof(*configs); ind++) {
    grpc_end2end_tests(argc, argv, configs[ind]);
  }
  grpc_shutdown();
  return 0;
}
