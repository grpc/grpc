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

#include "test/cpp/end2ened/spiffe_end2end_test/spiffe_test_credentials.h"
#include "test/core/end2end/data/ssl_test_data.h"

namespace grpc{
namespace testing {

/** An implementation of the schedule field in a
 * TlsCredentialReloadConfig instance, to be used for either the server or
 * client. This has the same functionality as the
 * method client_cred_reload_sync from h2_spiffe.cc. **/
static int CredentialReloadSync(void* config_user_data, ::grpc_impl::experimental::TlsCredentialReloadArg* arg) {
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

/** An implementation of the schedule field in a
 * TlsServerAuthorizationCheckConfig instance. This has the same functionality
 * as the method server_authz_check_cb from h2_spiffe.cc. **/
static int ServerAuthorizationCheckSync(void* config_user_data, ::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) {
  GPR_ASSERT(arg != nullptr);
  //arg->set_success(1);
  //arg->set_status(GRPC_STATUS_OK);
  //arg->OnServerAuthorizationCheckDoneCallback();
  return 0;
}

std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> CreateTestTlsCredentialsOptions(bool is_client) {
  /**
  std::shared_ptr<::grpc_impl::experimental::TlsKeyMaterialsConfig> test_key_materials_config(new ::grpc_impl::experimental::TlsKeyMaterialsConfig());
  struct ::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair pem_key_cert_pair = {test_server1_key, test_server1_cert};
  std::vector<::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair> pem_key_cert_pair_list;
  pem_key_cert_pair_list.push_back(pem_key_cert_pair);
  test_key_materials_config->set_key_materials(test_root_cert, pem_key_cert_pair_list);
  **/

  std::shared_ptr<::grpc_impl::experimental::TlsCredentialReloadConfig> test_credential_reload_config(
      new ::grpc_impl::experimental::TlsCredentialReloadConfig(nullptr, &CredentialReloadSync, nullptr, nullptr));
  std::shared_ptr<::grpc_impl::experimental::TlsServerAuthorizationCheckConfig> test_server_authorization_check_config(
      new ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig(nullptr, &ServerAuthorizationCheckSync, nullptr, nullptr));
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> options(new ::grpc_impl::experimental::TlsCredentialsOptions(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
      nullptr,
      test_credential_reload_config,
      is_client ? test_server_authorization_check_config : nullptr));
  return options;
}

} // namespace testing
} // namespace grpc
