// Copyright 2023 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_H2_OAUTH2_COMMON_H
#define GRPC_TEST_CORE_END2END_FIXTURES_H2_OAUTH2_COMMON_H

#include <string.h>

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
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"

class Oauth2Fixture : public SecureFixture {
 public:
  explicit Oauth2Fixture(grpc_tls_version tls_version)
      : tls_version_(tls_version) {}

  static const char* CaCertPath() { return "src/core/tsi/test_creds/ca.pem"; }
  static const char* ServerCertPath() {
    return "src/core/tsi/test_creds/server1.pem";
  }
  static const char* ServerKeyPath() {
    return "src/core/tsi/test_creds/server1.key";
  }

 private:
  struct TestProcessorState {};

  static const char* oauth2_md() { return "Bearer aaslkfjs424535asdf"; }
  static const char* client_identity_property_name() { return "smurf_name"; }
  static const char* client_identity() { return "Brainy Smurf"; }

  static const grpc_metadata* find_metadata(const grpc_metadata* md,
                                            size_t md_count, const char* key,
                                            const char* value) {
    size_t i;
    for (i = 0; i < md_count; i++) {
      if (grpc_slice_str_cmp(md[i].key, key) == 0 &&
          grpc_slice_str_cmp(md[i].value, value) == 0) {
        return &md[i];
      }
    }
    return nullptr;
  }

  static void process_oauth2_success(void*, grpc_auth_context*,
                                     const grpc_metadata* md, size_t md_count,
                                     grpc_process_auth_metadata_done_cb cb,
                                     void* user_data) {
    const grpc_metadata* oauth2 =
        find_metadata(md, md_count, "authorization", oauth2_md());
    GPR_ASSERT(oauth2 != nullptr);
    cb(user_data, oauth2, 1, nullptr, 0, GRPC_STATUS_OK, nullptr);
  }

  static void process_oauth2_failure(void* state, grpc_auth_context* /*ctx*/,
                                     const grpc_metadata* md, size_t md_count,
                                     grpc_process_auth_metadata_done_cb cb,
                                     void* user_data) {
    const grpc_metadata* oauth2 =
        find_metadata(md, md_count, "authorization", oauth2_md());
    GPR_ASSERT(state != nullptr);
    GPR_ASSERT(oauth2 != nullptr);
    cb(user_data, oauth2, 1, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
  }

  static grpc_auth_metadata_processor test_processor_create(bool failing) {
    auto* s = new TestProcessorState;
    grpc_auth_metadata_processor result;
    result.state = s;
    result.destroy = [](void* p) {
      delete static_cast<TestProcessorState*>(p);
    };
    if (failing) {
      result.process = process_oauth2_failure;
    } else {
      result.process = process_oauth2_success;
    }
    return result;
  }

  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "foo.test.google.fr");
  }

  grpc_channel_credentials* MakeClientCreds(
      const grpc_core::ChannelArgs&) override {
    grpc_slice ca_slice;
    GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                 grpc_load_file(CaCertPath(), 1, &ca_slice)));
    const char* test_root_cert =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
    grpc_channel_credentials* ssl_creds =
        grpc_ssl_credentials_create(test_root_cert, nullptr, nullptr, nullptr);
    if (ssl_creds != nullptr) {
      // Set the min and max TLS version.
      grpc_ssl_credentials* creds =
          reinterpret_cast<grpc_ssl_credentials*>(ssl_creds);
      creds->set_min_tls_version(tls_version_);
      creds->set_max_tls_version(tls_version_);
    }
    grpc_call_credentials* oauth2_creds =
        grpc_md_only_test_credentials_create("authorization", oauth2_md());
    grpc_channel_credentials* ssl_oauth2_creds =
        grpc_composite_channel_credentials_create(ssl_creds, oauth2_creds,
                                                  nullptr);
    grpc_channel_credentials_release(ssl_creds);
    grpc_call_credentials_release(oauth2_creds);
    grpc_slice_unref(ca_slice);
    return ssl_oauth2_creds;
  }

  grpc_server_credentials* MakeServerCreds(
      const grpc_core::ChannelArgs& args) override {
    grpc_slice cert_slice, key_slice;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(ServerCertPath(), 1, &cert_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(ServerKeyPath(), 1, &key_slice)));
    const char* server_cert =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
    const char* server_key =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key, server_cert};
    grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
        nullptr, &pem_key_cert_pair, 1, 0, nullptr);
    if (ssl_creds != nullptr) {
      // Set the min and max TLS version.
      grpc_ssl_server_credentials* creds =
          reinterpret_cast<grpc_ssl_server_credentials*>(ssl_creds);
      creds->set_min_tls_version(tls_version_);
      creds->set_max_tls_version(tls_version_);
    }
    grpc_server_credentials_set_auth_metadata_processor(
        ssl_creds,
        test_processor_create(args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)));
    grpc_slice_unref(cert_slice);
    grpc_slice_unref(key_slice);
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

  grpc_tls_version tls_version_;
};

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_H2_OAUTH2_COMMON_H
