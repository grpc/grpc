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
#include <grpc/grpc_security_constants.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/global_config_generic.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

static grpc_ssl_certificate_config_reload_status
ssl_server_certificate_config_callback(
    void* user_data, grpc_ssl_server_certificate_config** config) {
  if (config == nullptr) {
    return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL;
  }
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(user_data);
  if (!ffd->server_credential_reloaded) {
    grpc_slice ca_slice, cert_slice, key_slice;
    GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                 grpc_load_file(CA_CERT_PATH, 1, &ca_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
    const char* ca_cert =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
    const char* server_cert =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
    const char* server_key =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key, server_cert};
    *config = grpc_ssl_server_certificate_config_create(ca_cert,
                                                        &pem_key_cert_pair, 1);
    grpc_slice_unref(cert_slice);
    grpc_slice_unref(key_slice);
    grpc_slice_unref(ca_slice);
    ffd->server_credential_reloaded = true;
    return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
  } else {
    return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  }
}

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack_tls1_3(
    const grpc_channel_args* client_args,
    const grpc_channel_args* server_args) {
  return chttp2_create_fixture_secure_fullstack(client_args, server_args,
                                                grpc_tls_version::TLS1_3);
}

static void chttp2_init_client_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture* f, const grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
  if (f != nullptr && ssl_creds != nullptr) {
    // Set the min and max TLS version.
    grpc_ssl_credentials* creds =
        reinterpret_cast<grpc_ssl_credentials*>(ssl_creds);
    fullstack_secure_fixture_data* ffd =
        static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
    creds->set_min_tls_version(ffd->tls_version);
    creds->set_max_tls_version(ffd->tls_version);
  }
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  const grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);
  grpc_channel_args_destroy(new_client_args);
}

static void chttp2_init_server_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture* f, const grpc_channel_args* server_args) {
  grpc_ssl_server_credentials_options* options =
      grpc_ssl_server_credentials_create_options_using_config_fetcher(
          GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
          ssl_server_certificate_config_callback, f->fixture_data);
  grpc_server_credentials* ssl_creds =
      grpc_ssl_server_credentials_create_with_options(options);
  if (f != nullptr && ssl_creds != nullptr) {
    // Set the min and max TLS version.
    grpc_ssl_server_credentials* creds =
        reinterpret_cast<grpc_ssl_server_credentials*>(ssl_creds);
    fullstack_secure_fixture_data* ffd =
        static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
    creds->set_min_tls_version(ffd->tls_version);
    creds->set_max_tls_version(ffd->tls_version);
  }
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/simple_ssl_fullstack_tls1_3",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST,
     "foo.test.google.fr", chttp2_create_fixture_secure_fullstack_tls1_3,
     chttp2_init_client_simple_ssl_secure_fullstack,
     chttp2_init_server_simple_ssl_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
};

int main(int argc, char** argv) {
  size_t i;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, CA_CERT_PATH);

  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
