// Copyright 2015 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_H2_SSL_CRED_RELOAD_FIXTURE_H
#define GRPC_TEST_CORE_END2END_FIXTURES_H2_SSL_CRED_RELOAD_FIXTURE_H

#include <stddef.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"

class SslCredReloadFixture : public SecureFixture {
 public:
  explicit SslCredReloadFixture(grpc_tls_version tls_version)
      : tls_version_(tls_version) {}

  static const char* CaCertPath() { return "src/core/tsi/test_creds/ca.pem"; }
  static const char* CertPath() {
    return "src/core/tsi/test_creds/server1.pem";
  }
  static const char* KeyPath() { return "src/core/tsi/test_creds/server1.key"; }

 private:
  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "foo.test.google.fr");
  }
  grpc_channel_credentials* MakeClientCreds(
      const grpc_core::ChannelArgs&) override {
    grpc_channel_credentials* ssl_creds =
        grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
    if (ssl_creds != nullptr) {
      // Set the min and max TLS version.
      grpc_ssl_credentials* creds =
          reinterpret_cast<grpc_ssl_credentials*>(ssl_creds);
      creds->set_min_tls_version(tls_version_);
      creds->set_max_tls_version(tls_version_);
    }
    return ssl_creds;
  }
  grpc_server_credentials* MakeServerCreds(
      const grpc_core::ChannelArgs& args) override {
    server_credential_reloaded_ = false;
    grpc_ssl_server_credentials_options* options =
        grpc_ssl_server_credentials_create_options_using_config_fetcher(
            GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
            ssl_server_certificate_config_callback, this);
    grpc_server_credentials* ssl_creds =
        grpc_ssl_server_credentials_create_with_options(options);
    if (ssl_creds != nullptr) {
      // Set the min and max TLS version.
      grpc_ssl_server_credentials* creds =
          reinterpret_cast<grpc_ssl_server_credentials*>(ssl_creds);
      creds->set_min_tls_version(tls_version_);
      creds->set_max_tls_version(tls_version_);
    }
    if (args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)) {
      grpc_auth_metadata_processor processor = {process_auth_failure, nullptr,
                                                nullptr};
      grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
    }
    return ssl_creds;
  }

  static void process_auth_failure(void* state, grpc_auth_context* /*ctx*/,
                                   const grpc_metadata* /*md*/,
                                   size_t /*md_count*/,
                                   grpc_process_auth_metadata_done_cb cb,
                                   void* user_data) {
    GPR_ASSERT(state == nullptr);
    cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
  }

  grpc_ssl_certificate_config_reload_status SslServerCertificateConfigCallback(
      grpc_ssl_server_certificate_config** config) {
    if (config == nullptr) {
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL;
    }
    if (!server_credential_reloaded_) {
      grpc_slice ca_slice, cert_slice, key_slice;
      GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                   grpc_load_file(CaCertPath(), 1, &ca_slice)));
      GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                   grpc_load_file(CertPath(), 1, &cert_slice)));
      GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                   grpc_load_file(KeyPath(), 1, &key_slice)));
      const char* ca_cert =
          reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
      const char* server_cert =
          reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
      const char* server_key =
          reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
      grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key, server_cert};
      *config = grpc_ssl_server_certificate_config_create(
          ca_cert, &pem_key_cert_pair, 1);
      grpc_slice_unref(cert_slice);
      grpc_slice_unref(key_slice);
      grpc_slice_unref(ca_slice);
      server_credential_reloaded_ = true;
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
    } else {
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
    }
  }

  static grpc_ssl_certificate_config_reload_status
  ssl_server_certificate_config_callback(
      void* user_data, grpc_ssl_server_certificate_config** config) {
    return static_cast<SslCredReloadFixture*>(user_data)
        ->SslServerCertificateConfigCallback(config);
  }

  grpc_tls_version tls_version_;
  bool server_credential_reloaded_ = false;
};

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_H2_SSL_CRED_RELOAD_FIXTURE_H
