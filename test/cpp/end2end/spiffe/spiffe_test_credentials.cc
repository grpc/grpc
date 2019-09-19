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

#include "test/cpp/end2end/spiffe/spiffe_test_credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"

namespace grpc{
namespace testing {

class TestTlsCredentialReloadInterface : public ::grpc_impl::experimental::TlsCredentialReloadInterface {

int Schedule(::grpc_impl::experimental::TlsCredentialReloadArg* arg) override {
  GPR_ASSERT(arg != nullptr);
  std::shared_ptr<::grpc_impl::experimental::TlsKeyMaterialsConfig> arg_key_materials_config = arg->key_materials_config();
  GPR_ASSERT(arg_key_materials_config != nullptr);
  if (!arg_key_materials_config->pem_key_cert_pair_list().empty()) {
    arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED);
    return 0;
  }
  struct ::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair pem_key_cert_pair = {test_server1_key, test_server1_cert};
  std::vector<::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair> pem_key_cert_pair_list =
      arg_key_materials_config->pem_key_cert_pair_list();
  pem_key_cert_pair_list.push_back(pem_key_cert_pair);
  arg_key_materials_config->set_key_materials(test_root_cert, pem_key_cert_pair_list);
  arg->set_key_materials_config(arg_key_materials_config);
  arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  arg->OnCredentialReloadDoneCallback();
  return 0;
}

void Cancel(::grpc_impl::experimental::TlsCredentialReloadArg* arg) override {
  return;
}

void Release() override {
  return;
}
};

class TestTlsServerAuthorizationCheckInterface : public ::grpc_impl::experimental::TlsServerAuthorizationCheckInterface {

int Schedule(::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) override {
  GPR_ASSERT(arg != nullptr);
  //arg->set_success(1);
  //arg->set_status(GRPC_STATUS_OK);
  //arg->OnServerAuthorizationCheckDoneCallback();
  return 0;
}

void Cancel(::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) override {
  return;
}

void Release() override {
  return;
}
};

std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> CreateTestTlsCredentialsOptions(bool is_client) {
  /**
  std::shared_ptr<::grpc_impl::experimental::TlsKeyMaterialsConfig> test_key_materials_config(new ::grpc_impl::experimental::TlsKeyMaterialsConfig());
  struct ::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair pem_key_cert_pair = {test_server1_key, test_server1_cert};
  std::vector<::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair> pem_key_cert_pair_list;
  pem_key_cert_pair_list.push_back(pem_key_cert_pair);
  test_key_materials_config->set_key_materials(test_root_cert, pem_key_cert_pair_list);
  **/

  std::shared_ptr<TestTlsCredentialReloadInterface> credential_reload_interface(new TestTlsCredentialReloadInterface());
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialReloadConfig> test_credential_reload_config(new ::grpc_impl::experimental::TlsCredentialReloadConfig(credential_reload_interface));
  std::shared_ptr<TestTlsServerAuthorizationCheckInterface> server_authorization_check_interface(new TestTlsServerAuthorizationCheckInterface());
  std::shared_ptr<::grpc_impl::experimental::TlsServerAuthorizationCheckConfig> test_server_authorization_check_config(new ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig(server_authorization_check_interface));
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> options(new ::grpc_impl::experimental::TlsCredentialsOptions(
      is_client ? GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE : GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
      nullptr,
      test_credential_reload_config,
      is_client ? test_server_authorization_check_config : nullptr));
  return options;
}

} // namespace testing
} // namespace grpc
