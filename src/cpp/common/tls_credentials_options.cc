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

/** TLS key materials config API implementation **/
void TlsKeyMaterialsConfig::set_key_materials(
    grpc::string pem_root_certs,
    std::vector<PemKeyCertPair> pem_key_cert_pair_list) {
  pem_key_cert_pair_list_ = std::move(pem_key_cert_pair_list);
  pem_root_certs_ = std::move(pem_root_certs);
}

/** Creates a new C struct for the key materials. **/
grpc_tls_key_materials_config* c_key_materials(const std::shared_ptr<TlsKeyMaterialsConfig>& config) {
  grpc_tls_key_materials_config* c_config =
      grpc_tls_key_materials_config_create();
  ::grpc_core::InlinedVector<::grpc_core::PemKeyCertPair, 1>
      c_pem_key_cert_pair_list;
  for (auto key_cert_pair = config->pem_key_cert_pair_list().begin();
       key_cert_pair != config->pem_key_cert_pair_list().end(); key_cert_pair++) {
    grpc_ssl_pem_key_cert_pair* ssl_pair =
        (grpc_ssl_pem_key_cert_pair*)gpr_malloc(
            sizeof(grpc_ssl_pem_key_cert_pair));
    ssl_pair->private_key = gpr_strdup(key_cert_pair->private_key.c_str());
    ssl_pair->cert_chain = gpr_strdup(key_cert_pair->cert_chain.c_str());
    ::grpc_core::PemKeyCertPair c_pem_key_cert_pair =
        ::grpc_core::PemKeyCertPair(ssl_pair);
    c_pem_key_cert_pair_list.push_back(::std::move(c_pem_key_cert_pair));
  }
  ::grpc_core::UniquePtr<char> c_pem_root_certs(gpr_strdup(config->pem_root_certs().c_str()));
  c_config->set_key_materials(std::move(c_pem_root_certs),
                              std::move(c_pem_key_cert_pair_list));
  c_config->set_version(config->version());
  return c_config;
}

/** Creates a new TlsKeyMaterialsConfig from a C struct config. **/
std::shared_ptr<TlsKeyMaterialsConfig> tls_key_materials_c_to_cpp(
    const grpc_tls_key_materials_config* config) {
  std::shared_ptr<TlsKeyMaterialsConfig> cpp_config(
      new TlsKeyMaterialsConfig());
  std::vector<TlsKeyMaterialsConfig::PemKeyCertPair>
      cpp_pem_key_cert_pair_list;
  grpc_tls_key_materials_config::PemKeyCertPairList pem_key_cert_pair_list =
      config->pem_key_cert_pair_list();
  for (size_t i = 0; i < pem_key_cert_pair_list.size(); i++) {
    ::grpc_core::PemKeyCertPair key_cert_pair = pem_key_cert_pair_list[i];
    TlsKeyMaterialsConfig::PemKeyCertPair p = {
        gpr_strdup(key_cert_pair.private_key()),
        gpr_strdup(key_cert_pair.cert_chain())};
    cpp_pem_key_cert_pair_list.push_back(::std::move(p));
  }
  cpp_config->set_key_materials(
      std::move(gpr_strdup(config->pem_root_certs())),
      std::move(cpp_pem_key_cert_pair_list));
  cpp_config->set_version(config->version());
  return cpp_config;
}

/** TLS credential reload arg API implementation **/
TlsCredentialReloadArg::TlsCredentialReloadArg() {}

TlsCredentialReloadArg::TlsCredentialReloadArg(
    grpc_tls_credential_reload_arg arg) {
  c_arg_ = arg;
}

TlsCredentialReloadArg::~TlsCredentialReloadArg() {}

void* TlsCredentialReloadArg::cb_user_data() const {
  return c_arg_.cb_user_data;
}

/** This function creates a new TlsKeyMaterialsConfig instance whose fields are
 * not shared with the corresponding key materials config fields of the
 * TlsCredentialReloadArg instance. **/
std::shared_ptr<TlsKeyMaterialsConfig> TlsCredentialReloadArg::key_materials_config() const {
  return tls_key_materials_c_to_cpp(c_arg_.key_materials_config);
}

grpc_ssl_certificate_config_reload_status TlsCredentialReloadArg::status() const {
  return c_arg_.status;
}

std::shared_ptr<grpc::string> TlsCredentialReloadArg::error_details() const {
  std::shared_ptr<grpc::string> cpp_error_details(new grpc::string(c_arg_.error_details));
  return cpp_error_details;
}

void TlsCredentialReloadArg::set_cb_user_data(void* cb_user_data) {
  c_arg_.cb_user_data = cb_user_data;
}

void TlsCredentialReloadArg::set_key_materials_config(
    std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config) {
  c_arg_.key_materials_config = c_key_materials(key_materials_config);
}

void TlsCredentialReloadArg::set_status(
    grpc_ssl_certificate_config_reload_status status) {
  c_arg_.status = status;
}

void TlsCredentialReloadArg::set_error_details(const grpc::string& error_details) {
  c_arg_.error_details = gpr_strdup(error_details.c_str());
}

void TlsCredentialReloadArg::callback() {
  c_arg_.cb(&c_arg_);
}

/** The C schedule and cancel functions for the credential reload config. **/
int tls_credential_reload_config_c_schedule(
    void* config_user_data, grpc_tls_credential_reload_arg* arg) {
  TlsCredentialReloadConfig* cpp_config =
      static_cast<TlsCredentialReloadConfig*>(arg->config->context());
  TlsCredentialReloadArg cpp_arg(*arg);
  int schedule_output = cpp_config->Schedule(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->key_materials_config = c_key_materials(cpp_arg.key_materials_config());
  arg->status = cpp_arg.status();
  arg->error_details = gpr_strdup(cpp_arg.error_details()->c_str());
  return schedule_output;
}

void tls_credential_reload_config_c_cancel(
    void* config_user_data, grpc_tls_credential_reload_arg* arg) {
  TlsCredentialReloadConfig* cpp_config =
      static_cast<TlsCredentialReloadConfig*>(arg->config->context());
  TlsCredentialReloadArg cpp_arg(*arg);
  cpp_config->Cancel(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->key_materials_config = c_key_materials(cpp_arg.key_materials_config());
  arg->status = cpp_arg.status();
  arg->error_details = cpp_arg.error_details()->c_str();
}

/** gRPC TLS credential reload config API implementation **/
TlsCredentialReloadConfig::TlsCredentialReloadConfig(
    const void* config_user_data,
    int (*schedule)(void* config_user_data, TlsCredentialReloadArg* arg),
    void (*cancel)(void* config_user_data, TlsCredentialReloadArg* arg),
    void (*destruct)(void* config_user_data)) {
  config_user_data_ = const_cast<void*>(config_user_data);
  schedule_ = schedule;
  cancel_ = cancel;
  destruct_ = destruct;
  c_config_ = grpc_tls_credential_reload_config_create(
      config_user_data_, &tls_credential_reload_config_c_schedule,
      &tls_credential_reload_config_c_cancel, destruct_);
  c_config_->set_context(static_cast<void*>(this));
}

TlsCredentialReloadConfig::~TlsCredentialReloadConfig() {}

/** gRPC TLS server authorization check arg API implementation **/
TlsServerAuthorizationCheckArg::TlsServerAuthorizationCheckArg() {}

TlsServerAuthorizationCheckArg::TlsServerAuthorizationCheckArg(
    grpc_tls_server_authorization_check_arg arg) {
  c_arg_ = arg;
}

TlsServerAuthorizationCheckArg::~TlsServerAuthorizationCheckArg() {}

void* TlsServerAuthorizationCheckArg::cb_user_data() const {
  return c_arg_.cb_user_data;
}

int TlsServerAuthorizationCheckArg::success() const { return c_arg_.success; }

std::shared_ptr<grpc::string> TlsServerAuthorizationCheckArg::target_name()
    const {
  std::shared_ptr<grpc::string> cpp_target_name(
      new grpc::string(c_arg_.target_name));
  return cpp_target_name;
}

std::shared_ptr<grpc::string> TlsServerAuthorizationCheckArg::peer_cert()
    const {
  std::shared_ptr<grpc::string> cpp_peer_cert(
      new grpc::string(c_arg_.peer_cert));
  return cpp_peer_cert;
}

grpc_status_code TlsServerAuthorizationCheckArg::status() const {
  return c_arg_.status;
}

std::shared_ptr<grpc::string> TlsServerAuthorizationCheckArg::error_details()
    const {
  std::shared_ptr<grpc::string> cpp_error_details(
new grpc::string(c_arg_.error_details));
  return cpp_error_details;
}

void TlsServerAuthorizationCheckArg::set_cb_user_data(void* cb_user_data) {
  c_arg_.cb_user_data = cb_user_data;
}

void TlsServerAuthorizationCheckArg::set_success(int success) {
  c_arg_.success = success;
}

void TlsServerAuthorizationCheckArg::set_target_name(
    const grpc::string& target_name) {
  c_arg_.target_name = gpr_strdup(target_name.c_str());
}

void TlsServerAuthorizationCheckArg::set_peer_cert(
    const grpc::string& peer_cert) {
  c_arg_.peer_cert = gpr_strdup(peer_cert.c_str());
}

void TlsServerAuthorizationCheckArg::set_status(grpc_status_code status) {
  c_arg_.status = status;
}

void TlsServerAuthorizationCheckArg::set_error_details(
    const grpc::string& error_details) {
  c_arg_.error_details = gpr_strdup(error_details.c_str());
}

/** The C schedule and cancel functions for the credential reload config. **/
int tls_server_authorization_check_config_c_schedule(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg) {
  TlsServerAuthorizationCheckConfig* cpp_config =
      static_cast<TlsServerAuthorizationCheckConfig*>(arg->config->context());
  TlsServerAuthorizationCheckArg cpp_arg(*arg);
  int schedule_output = cpp_config->Schedule(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->success = cpp_arg.success();
  arg->target_name = gpr_strdup(cpp_arg.target_name()->c_str());
  arg->peer_cert = gpr_strdup(cpp_arg.peer_cert()->c_str());
  arg->status = cpp_arg.status();
  arg->error_details = gpr_strdup(cpp_arg.error_details()->c_str());
  return schedule_output;
}

void tls_server_authorization_check_config_c_cancel(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg) {
  TlsServerAuthorizationCheckConfig* cpp_config =
      static_cast<TlsServerAuthorizationCheckConfig*>(arg->config->context());
  TlsServerAuthorizationCheckArg cpp_arg(*arg);
  cpp_config->Cancel(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->success = cpp_arg.success();
  arg->target_name = gpr_strdup(cpp_arg.target_name()->c_str());
  arg->peer_cert = gpr_strdup(cpp_arg.peer_cert()->c_str());
  arg->status = cpp_arg.status();
  arg->error_details = gpr_strdup(cpp_arg.error_details()->c_str());
}

/** gRPC TLS server authorization check config API implementation **/
TlsServerAuthorizationCheckConfig::TlsServerAuthorizationCheckConfig(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    TlsServerAuthorizationCheckArg* arg),
    void (*cancel)(void* config_user_data, TlsServerAuthorizationCheckArg* arg),
    void (*destruct)(void* config_user_data)) {
  config_user_data_ = const_cast<void*>(config_user_data);
  schedule_ = schedule;
  cancel_ = cancel;
destruct_ = destruct;
  c_config_ = grpc_tls_server_authorization_check_config_create(
      config_user_data_, &tls_server_authorization_check_config_c_schedule,
      &tls_server_authorization_check_config_c_cancel, destruct_);
  c_config_->set_context(static_cast<void*>(this));
}


TlsServerAuthorizationCheckConfig::~TlsServerAuthorizationCheckConfig() {}

/** gRPC TLS credential options API implementation **/
grpc_tls_credentials_options* TlsCredentialsOptions::c_credentials_options()
    const {
  grpc_tls_credentials_options* c_options =
      grpc_tls_credentials_options_create();
  c_options->set_cert_request_type(cert_request_type_);
  c_options->set_key_materials_config(
      ::grpc_core::RefCountedPtr<grpc_tls_key_materials_config>(c_key_materials(key_materials_config_)));
  c_options->set_credential_reload_config(
      ::grpc_core::RefCountedPtr<grpc_tls_credential_reload_config>(
          credential_reload_config_->c_credential_reload()));
  c_options->set_server_authorization_check_config(
      ::grpc_core::RefCountedPtr<grpc_tls_server_authorization_check_config>(
          server_authorization_check_config_->c_server_authorization_check()));
  return c_options;
}

}  // namespace experimental
}  // namespace grpc_impl
