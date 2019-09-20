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
 
#include "src/cpp/common/tls_credentials_options_util.h"
 
namespace grpc_impl {
namespace experimental {
 
/** TLS key materials config API implementation **/
void TlsKeyMaterialsConfig::set_key_materials(
    grpc::string pem_root_certs,
    std::vector<PemKeyCertPair> pem_key_cert_pair_list) {
  pem_key_cert_pair_list_ = std::move(pem_key_cert_pair_list);
  pem_root_certs_ = std::move(pem_root_certs);
}
 
/** TLS credential reload arg API implementation **/
TlsCredentialReloadArg::~TlsCredentialReloadArg() {}
 
void* TlsCredentialReloadArg::cb_user_data() const {
  return c_arg_->cb_user_data;
}
 
/** This function creates a new TlsKeyMaterialsConfig instance whose fields are
 * not shared with the corresponding key materials config fields of the
 * TlsCredentialReloadArg instance. **/
std::shared_ptr<TlsKeyMaterialsConfig>
TlsCredentialReloadArg::key_materials_config() const {
  return ConvertToCppKeyMaterialsConfig(c_arg_->key_materials_config);
}
 
grpc_ssl_certificate_config_reload_status TlsCredentialReloadArg::status()
    const {
  return c_arg_->status;
}
 
grpc::string TlsCredentialReloadArg::error_details() const {
  grpc::string cpp_error_details(c_arg_->error_details);
  return cpp_error_details;
}
 
void TlsCredentialReloadArg::set_cb_user_data(void* cb_user_data) {
  c_arg_->cb_user_data = cb_user_data;
}
 
void TlsCredentialReloadArg::set_key_materials_config(
    const std::shared_ptr<TlsKeyMaterialsConfig>& key_materials_config) {
  c_arg_->key_materials_config =
      ConvertToCKeyMaterialsConfig(key_materials_config);
}
 
void TlsCredentialReloadArg::set_status(
    grpc_ssl_certificate_config_reload_status status) {
  c_arg_->status = status;
}
 
void TlsCredentialReloadArg::set_error_details(
    const grpc::string& error_details) {
  c_arg_->error_details = gpr_strdup(error_details.c_str());
}
 
void TlsCredentialReloadArg::OnCredentialReloadDoneCallback() {
  if (c_arg_->cb == nullptr) {
    gpr_log(GPR_ERROR, "credential reload arg callback API is nullptr");
    return;
  }
  c_arg_->cb(c_arg_);
}
 
/** gRPC TLS credential reload config API implementation **/
TlsCredentialReloadConfig::TlsCredentialReloadConfig(
    std::unique_ptr<TlsCredentialReloadInterface> credential_reload_interface)
    : credential_reload_interface_(std::move(credential_reload_interface)) {
  c_config_ = grpc_tls_credential_reload_config_create(
      nullptr, &TlsCredentialReloadConfigCSchedule,
      &TlsCredentialReloadConfigCCancel, nullptr);
  c_config_->set_context(static_cast<void*>(this));
}
 
TlsCredentialReloadConfig::~TlsCredentialReloadConfig() {
  if (credential_reload_interface_ != nullptr) {
    credential_reload_interface_->Release();
  }
}
 
/** gRPC TLS server authorization check arg API implementation **/
TlsServerAuthorizationCheckArg::~TlsServerAuthorizationCheckArg() {}
 
void* TlsServerAuthorizationCheckArg::cb_user_data() const {
  return c_arg_->cb_user_data;
}
 
int TlsServerAuthorizationCheckArg::success() const { return c_arg_->success; }
 
grpc::string TlsServerAuthorizationCheckArg::target_name() const {
  grpc::string cpp_target_name(c_arg_->target_name);
  return cpp_target_name;
}
 
grpc::string TlsServerAuthorizationCheckArg::peer_cert() const {
  grpc::string cpp_peer_cert(c_arg_->peer_cert);
  return cpp_peer_cert;
}
 
grpc_status_code TlsServerAuthorizationCheckArg::status() const {
  return c_arg_->status;
}
 
grpc::string TlsServerAuthorizationCheckArg::error_details() const {
  grpc::string cpp_error_details(c_arg_->error_details);
  return cpp_error_details;
}
 
void TlsServerAuthorizationCheckArg::set_cb_user_data(void* cb_user_data) {
  c_arg_->cb_user_data = cb_user_data;
}
 
void TlsServerAuthorizationCheckArg::set_success(int success) {
  c_arg_->success = success;
}
 
void TlsServerAuthorizationCheckArg::set_target_name(
    const grpc::string& target_name) {
  c_arg_->target_name = gpr_strdup(target_name.c_str());
}
 
void TlsServerAuthorizationCheckArg::set_peer_cert(
    const grpc::string& peer_cert) {
  c_arg_->peer_cert = gpr_strdup(peer_cert.c_str());
}
 
void TlsServerAuthorizationCheckArg::set_status(grpc_status_code status) {
  c_arg_->status = status;
}
 
void TlsServerAuthorizationCheckArg::set_error_details(
    const grpc::string& error_details) {
  c_arg_->error_details = gpr_strdup(error_details.c_str());
}
 
void TlsServerAuthorizationCheckArg::OnServerAuthorizationCheckDoneCallback() {
  if (c_arg_->cb == nullptr) {
    gpr_log(GPR_ERROR, "server authorizaton check arg callback API is nullptr");
    return;
  }
  c_arg_->cb(c_arg_);
}
 
/** gRPC TLS server authorization check config API implementation. **/
TlsServerAuthorizationCheckConfig::TlsServerAuthorizationCheckConfig(
    std::unique_ptr<TlsServerAuthorizationCheckInterface>
        server_authorization_check_interface)
    : server_authorization_check_interface_(
          std::move(server_authorization_check_interface)) {
  c_config_ = grpc_tls_server_authorization_check_config_create(
      nullptr, &TlsServerAuthorizationCheckConfigCSchedule,
      &TlsServerAuthorizationCheckConfigCCancel, nullptr);
  c_config_->set_context(static_cast<void*>(this));
}
 
TlsServerAuthorizationCheckConfig::~TlsServerAuthorizationCheckConfig() {
  if (server_authorization_check_interface_ != nullptr) {
    server_authorization_check_interface_->Release();
  }
}
 
/** gRPC TLS credential options API implementation **/
TlsCredentialsOptions::TlsCredentialsOptions(
    grpc_ssl_client_certificate_request_type cert_request_type,
    std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config,
    std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config,
    std::shared_ptr<TlsServerAuthorizationCheckConfig>
        server_authorization_check_config)
    : cert_request_type_(cert_request_type),
      key_materials_config_(std::move(key_materials_config)),
      credential_reload_config_(std::move(credential_reload_config)),
      server_authorization_check_config_(
          std::move(server_authorization_check_config)) {
  c_credentials_options_ = grpc_tls_credentials_options_create();
  grpc_tls_credentials_options_set_cert_request_type(c_credentials_options_,
                                                     cert_request_type_);
  if (key_materials_config_ != nullptr) {
    grpc_tls_credentials_options_set_key_materials_config(
        c_credentials_options_,
        ConvertToCKeyMaterialsConfig(key_materials_config_));
  }
  if (credential_reload_config_ != nullptr) {
    grpc_tls_credentials_options_set_credential_reload_config(
        c_credentials_options_, credential_reload_config_->c_config());
  }
  if (server_authorization_check_config_ != nullptr) {
    grpc_tls_credentials_options_set_server_authorization_check_config(
        c_credentials_options_, server_authorization_check_config_->c_config());
  }
}
 
TlsCredentialsOptions::~TlsCredentialsOptions() {}
 
}  // namespace experimental
}  // namespace grpc_impl
