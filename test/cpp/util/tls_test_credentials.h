/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPC_TEST_CPP_UTIL_TLS_TEST_CREDENTIALS_H
#define GRPC_TEST_CPP_UTIL_TLS_TEST_CREDENTIALS_H

#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <memory>
#include <thread>
#include <vector>

using ::grpc_impl::experimental::TlsCredentialReloadConfig;
using ::grpc_impl::experimental::TlsCredentialsOptions;
using ::grpc_impl::experimental::TlsKeyMaterialsConfig;
using ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig;

namespace grpc {
namespace testing {

struct TlsThread {
  void Join() {
    if (thread_started_) {
      thread_.join();
      thread_started_ = false;
    }
  }

  std::thread thread_;
  bool thread_started_ = false;
};

class TlsData {
 public:
  TlsData(grpc_ssl_client_certificate_request_type cert_request_type,
          std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config,
          std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config,
          std::shared_ptr<TlsServerAuthorizationCheckConfig>
              server_authorization_check_config,
          std::vector<TlsThread*>* list)
      : options(cert_request_type, key_materials_config,
                credential_reload_config, server_authorization_check_config),
        thread_list(list), key_materials(key_materials_config), credential_reload(credential_reload_config), server_authorization_check(server_authorization_check_config) {}

  ~TlsData() {}

  TlsCredentialsOptions options;
  std::vector<TlsThread*>* thread_list = nullptr;
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials;
  std::shared_ptr<TlsCredentialReloadConfig> credential_reload;
  std::shared_ptr<TlsServerAuthorizationCheckConfig> server_authorization_check;
};

/** This method creates a TlsCredentialsOptions instance with no key materials,
 *  whose credential reload config is configured using the
 *  TestSyncTlsCredentialReload class, and whose server authorization check
 *  config is determined as follows:
 *  - if |is_client| is true,
 *      -if |is_async|, configured by TestAsyncTlsServerAuthorizationCheck,
 *      -otherwise, configured by TestSyncTlsServerAuthorizationCheck.
 *  - otherwise, the server authorization check config is not populated.
 *
 *  Further, the cert request type of the options instance is always set to
 *  GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY for both the
 *  client and the server. **/
TlsData* CreateTestTlsCredentialsOptions(bool is_client, bool is_async);

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_TLS_TEST_CREDENTIALS_H
