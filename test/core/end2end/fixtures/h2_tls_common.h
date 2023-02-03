//
//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_H2_TLS_COMMON_H
#define GRPC_TEST_CORE_END2END_FIXTURES_H2_TLS_COMMON_H

#include <stdint.h>
#include <string.h>

#include <string>

#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/tls_utils.h"

// For normal TLS connections.
#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

struct SecurityPrimitives {
  enum ProviderType { STATIC_PROVIDER = 0, FILE_PROVIDER = 1 } provider_type;
  enum VerifierType {
    EXTERNAL_SYNC_VERIFIER = 0,
    EXTERNAL_ASYNC_VERIFIER = 1,
    HOSTNAME_VERIFIER = 2
  } verifier_type;
  enum TlsVersion { V_12 = 0, V_13 = 1 } tls_version;
};

struct fullstack_secure_fixture_data {
  ~fullstack_secure_fixture_data() {
    grpc_tls_certificate_provider_release(client_provider);
    grpc_tls_certificate_provider_release(server_provider);
    grpc_tls_certificate_verifier_release(client_verifier);
    grpc_tls_certificate_verifier_release(server_verifier);
  }
  std::string localaddr;
  grpc_tls_version tls_version;
  grpc_tls_certificate_provider* client_provider = nullptr;
  grpc_tls_certificate_provider* server_provider = nullptr;
  grpc_tls_certificate_verifier* client_verifier = nullptr;
  grpc_tls_certificate_verifier* server_verifier = nullptr;
  bool check_call_host = true;
};

inline void SetTlsVersion(fullstack_secure_fixture_data* ffd,
                          SecurityPrimitives::TlsVersion tls_version) {
  switch (tls_version) {
    case SecurityPrimitives::TlsVersion::V_12: {
      ffd->tls_version = grpc_tls_version::TLS1_2;
      break;
    }
    case SecurityPrimitives::TlsVersion::V_13: {
      ffd->tls_version = grpc_tls_version::TLS1_3;
      break;
    }
  }
}

inline void SetCertificateProvider(
    fullstack_secure_fixture_data* ffd,
    SecurityPrimitives::ProviderType provider_type) {
  switch (provider_type) {
    case SecurityPrimitives::ProviderType::STATIC_PROVIDER: {
      grpc_slice root_slice, cert_slice, key_slice;
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "load_file", grpc_load_file(CA_CERT_PATH, 1, &root_slice)));
      std::string root_cert =
          std::string(grpc_core::StringViewFromSlice(root_slice));
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
      std::string identity_cert =
          std::string(grpc_core::StringViewFromSlice(cert_slice));
      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "load_file", grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
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
      grpc_slice_unref(root_slice);
      grpc_slice_unref(cert_slice);
      grpc_slice_unref(key_slice);
      break;
    }
    case SecurityPrimitives::ProviderType::FILE_PROVIDER: {
      ffd->client_provider = grpc_tls_certificate_provider_file_watcher_create(
          SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
      ffd->server_provider = grpc_tls_certificate_provider_file_watcher_create(
          SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
      break;
    }
  }
}

inline void SetCertificateVerifier(
    fullstack_secure_fixture_data* ffd,
    SecurityPrimitives::VerifierType verifier_type) {
  switch (verifier_type) {
    case SecurityPrimitives::VerifierType::EXTERNAL_SYNC_VERIFIER: {
      auto* client_sync_verifier =
          new grpc_core::testing::SyncExternalVerifier(true);
      ffd->client_verifier = grpc_tls_certificate_verifier_external_create(
          client_sync_verifier->base());
      auto* server_sync_verifier =
          new grpc_core::testing::SyncExternalVerifier(true);
      ffd->server_verifier = grpc_tls_certificate_verifier_external_create(
          server_sync_verifier->base());
      ffd->check_call_host = false;
      break;
    }
    case SecurityPrimitives::VerifierType::EXTERNAL_ASYNC_VERIFIER: {
      auto* client_async_verifier =
          new grpc_core::testing::AsyncExternalVerifier(true);
      ffd->client_verifier = grpc_tls_certificate_verifier_external_create(
          client_async_verifier->base());
      auto* server_async_verifier =
          new grpc_core::testing::AsyncExternalVerifier(true);
      ffd->server_verifier = grpc_tls_certificate_verifier_external_create(
          server_async_verifier->base());
      ffd->check_call_host = false;
      break;
    }
    case SecurityPrimitives::VerifierType::HOSTNAME_VERIFIER: {
      ffd->client_verifier = grpc_tls_certificate_verifier_host_name_create();
      // Hostname verifier couldn't be applied to the server side, so we will
      // use sync external verifier here.
      auto* server_async_verifier =
          new grpc_core::testing::AsyncExternalVerifier(true);
      ffd->server_verifier = grpc_tls_certificate_verifier_external_create(
          server_async_verifier->base());
      break;
    }
  }
}

inline void process_auth_failure(void* state, grpc_auth_context* /*ctx*/,
                                 const grpc_metadata* /*md*/,
                                 size_t /*md_count*/,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void* user_data) {
  GPR_ASSERT(state == nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

inline void chttp2_init_client_secure_fullstack(
    grpc_end2end_test_fixture* f, const grpc_channel_args* client_args,
    grpc_channel_credentials* creds) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  f->client = grpc_channel_create(ffd->localaddr.c_str(), creds, client_args);
  GPR_ASSERT(f->client != nullptr);
  grpc_channel_credentials_release(creds);
}

inline void chttp2_init_server_secure_fullstack(
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

inline void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_secure_fixture_data* ffd =
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data);
  delete ffd;
}

// Create a TLS channel credential.
inline grpc_channel_credentials* create_tls_channel_credentials(
    fullstack_secure_fixture_data* ffd) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  grpc_tls_credentials_options_set_verify_server_cert(
      options, 1 /* = verify server certs */);
  options->set_min_tls_version(ffd->tls_version);
  options->set_max_tls_version(ffd->tls_version);
  // Set credential provider.
  grpc_tls_credentials_options_set_certificate_provider(options,
                                                        ffd->client_provider);
  grpc_tls_credentials_options_watch_root_certs(options);
  grpc_tls_credentials_options_watch_identity_key_cert_pairs(options);
  // Set credential verifier.
  grpc_tls_credentials_options_set_certificate_verifier(options,
                                                        ffd->client_verifier);
  grpc_tls_credentials_options_set_check_call_host(options,
                                                   ffd->check_call_host);
  // Create TLS channel credentials.
  grpc_channel_credentials* creds = grpc_tls_credentials_create(options);
  return creds;
}

// Create a TLS server credential.
inline grpc_server_credentials* create_tls_server_credentials(
    fullstack_secure_fixture_data* ffd) {
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  options->set_min_tls_version(ffd->tls_version);
  options->set_max_tls_version(ffd->tls_version);
  // Set credential provider.
  grpc_tls_credentials_options_set_certificate_provider(options,
                                                        ffd->server_provider);
  grpc_tls_credentials_options_watch_root_certs(options);
  grpc_tls_credentials_options_watch_identity_key_cert_pairs(options);
  // Set client certificate request type.
  grpc_tls_credentials_options_set_cert_request_type(
      options, GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  // Set credential verifier.
  grpc_tls_credentials_options_set_certificate_verifier(options,
                                                        ffd->server_verifier);
  grpc_server_credentials* creds = grpc_tls_server_credentials_create(options);
  return creds;
}

inline void chttp2_init_client(grpc_end2end_test_fixture* f,
                               const grpc_channel_args* client_args) {
  grpc_channel_credentials* ssl_creds = create_tls_channel_credentials(
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data));
  grpc_arg ssl_name_override = {
      GRPC_ARG_STRING,
      const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
      {const_cast<char*>("foo.test.google.fr")}};
  const grpc_channel_args* new_client_args =
      grpc_channel_args_copy_and_add(client_args, &ssl_name_override, 1);
  chttp2_init_client_secure_fullstack(f, new_client_args, ssl_creds);
  grpc_channel_args_destroy(new_client_args);
}

inline int fail_server_auth_check(const grpc_channel_args* server_args) {
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

inline void chttp2_init_server(grpc_end2end_test_fixture* f,
                               const grpc_channel_args* server_args) {
  grpc_server_credentials* ssl_creds = create_tls_server_credentials(
      static_cast<fullstack_secure_fixture_data*>(f->fixture_data));
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                              nullptr};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

static const uint32_t kH2TLSFeatureMask =
    FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
    FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS |
    FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
    FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER;

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_H2_TLS_COMMON_H
