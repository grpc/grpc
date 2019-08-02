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

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

namespace grpc_impl {
namespace experimental {

/** gRPC TLS key materials config API implementation **/
void TlsKeyMaterialsConfig::set_key_materials(
    ::grpc::string pem_root_certs,
    ::std::vector<PemKeyCertPair> pem_key_cert_pair_list) {
  pem_key_cert_pair_list_ = ::std::move(pem_key_cert_pair_list);
  pem_root_certs_ = ::std::move(pem_root_certs);
}

grpc_tls_key_materials_config* TlsKeyMaterialsConfig::c_key_materials() const {
  // TODO: implement.
  return nullptr;
}

/** gRPC TLS credential reload arg API implementation **/
void TlsCredentialReloadArg::set_cb(
    grpcpp_tls_on_credential_reload_done_cb cb) {
  cb_ = cb;
}

void TlsCredentialReloadArg::set_cb_user_data(void* cb_user_data) {
  cb_user_data_ = cb_user_data;
}

void TlsCredentialReloadArg::set_key_materials_config(
    ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config) {
  key_materials_config_ = key_materials_config;
}

void TlsCredentialReloadArg::set_status(
    grpc_ssl_certificate_config_reload_status status) {
  status_ = status;
}

void TlsCredentialReloadArg::set_error_details(::grpc::string error_details) {
  error_details_ = error_details;
}

grpc_tls_credential_reload_arg*
TlsCredentialReloadArg::c_credential_reload_arg() const {
  // TODO: implement.
  return nullptr;
}

/** gRPC TLS credential reload config API implementation **/
TlsCredentialReloadConfig::TlsCredentialReloadConfig(
    const void* config_user_data,
    int (*schedule)(void* config_user_data, TlsCredentialReloadArg* arg),
    void (*cancel)(void* config_user_data, TlsCredentialReloadArg* arg),
    void (*destruct)(void* config_user_data))
    : config_user_data_(const_cast<void*>(config_user_data)),
      schedule_(schedule),
      cancel_(cancel),
      destruct_(destruct) {}

TlsCredentialReloadConfig::~TlsCredentialReloadConfig() {}

grpc_tls_credential_reload_config*
TlsCredentialReloadConfig::c_credential_reload() const {
  // TODO: implement
  return nullptr;
}

/** gRPC TLS server authorization check arg API implementation **/
grpc_tls_server_authorization_check_arg*
TlsServerAuthorizationCheckArg::c_server_authorization_check_arg() const {
  // TODO: implement
  return nullptr;
}

/** gRPC TLS server authorization check config API implementation **/
TlsServerAuthorizationCheckConfig::TlsServerAuthorizationCheckConfig(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    TlsServerAuthorizationCheckArg* arg),
    void (*cancel)(void* config_user_data, TlsServerAuthorizationCheckArg* arg),
    void (*destruct)(void* config_user_data))
    : config_user_data_(const_cast<void*>(config_user_data)),
      schedule_(schedule),
      cancel_(cancel),
      destruct_(destruct) {}

TlsServerAuthorizationCheckConfig::~TlsServerAuthorizationCheckConfig() {}

grpc_tls_server_authorization_check_config*
TlsServerAuthorizationCheckConfig::c_server_authorization_check() const {
  // TODO: implement
  return nullptr;
}

/** gRPC TLS credential options API implementation **/
grpc_tls_credentials_options* TlsCredentialsOptions::c_credentials_options()
    const {
  grpc_tls_credentials_options* c_options =
      grpc_tls_credentials_options_create();
  c_options->set_cert_request_type(cert_request_type_);
  // TODO: put in C configs into functions below.
  c_options->set_key_materials_config(nullptr);
  c_options->set_credential_reload_config(nullptr);
  c_options->set_server_authorization_check_config(nullptr);
  return c_options;
}

}  // namespace experimental
}  // namespace grpc_impl
