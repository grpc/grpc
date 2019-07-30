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

#include <grpcpp/security/tls_credentials_options.h>

namespace grpc_impl {
namespace experimental {

/** gRPC TLS key materials config API implementation **/
void TlsKeyMaterialsConfig::set_key_materials(
    ::grpc::string pem_root_certs,
    ::std::vector<PemKeyCertPair> pem_key_cert_pair_list) {
  pem_key_cert_pair_list_ = ::std::move(pem_key_cert_pair_list);
  pem_root_certs_ = ::std::move(pem_root_certs);
}

/** gRPC TLS credential options API implementation **/
grpc_tls_credentials_options* TlsCredentialsOptions::c_credentials_options() const {
  grpc_tls_credentials_options* c_options = grpc_tls_credentials_options_create();
  c_options->set_cert_request_type(cert_request_type_);
  c_options->set_key_materials_config(key_materials_config_->c_key_materials());
  c_options->set_credential_reload_config(credential_reload_config_->c_credential_reload());
  c_options->set_server_authorization_check_config(
      server_authorization_check_config_->c_server_authorization_check());
  return c_options;
}

} // namespace experimental
} // namespace grpc_impl

