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
  grpc_tls_key_materials_config* c_config =
      grpc_tls_key_materials_config_create();
  ::grpc_core::InlinedVector<::grpc_core::PemKeyCertPair, 1>
      c_pem_key_cert_pair_list;
  for (auto key_cert_pair = pem_key_cert_pair_list_.begin();
       key_cert_pair != pem_key_cert_pair_list_.end(); key_cert_pair++) {
    grpc_ssl_pem_key_cert_pair p = {key_cert_pair->private_key.c_str(),
                                    key_cert_pair->cert_chain.c_str()};
    ::grpc_core::PemKeyCertPair c_pem_key_cert_pair =
        ::grpc_core::PemKeyCertPair(&p);
    c_pem_key_cert_pair_list.push_back(::std::move(c_pem_key_cert_pair));
  }
  ::grpc_core::UniquePtr<char> c_pem_root_certs(
      gpr_strdup(pem_root_certs_.c_str()));
  c_config->set_key_materials(::std::move(c_pem_root_certs),
                              ::std::move(c_pem_key_cert_pair_list));
  return c_config;
}

/** Creates smart pointer to a C++ version of the C key materials. **/
::std::shared_ptr<TlsKeyMaterialsConfig> cpp_key_materials(
    const grpc_tls_key_materials_config* config) {
  ::std::shared_ptr<TlsKeyMaterialsConfig> cpp_config(
      new TlsKeyMaterialsConfig());
  ::std::vector<TlsKeyMaterialsConfig::PemKeyCertPair>
      cpp_pem_key_cert_pair_list;
  /** for (auto key_cert_pair = config->pem_key_cert_pair_list().begin();
       key_cert_pair != config->pem_key_cert_pair_list().end(); key_cert_pair++)
  { TlsKeyMaterialsConfig::PemKeyCertPair p = {key_cert_pair->private_key,
  key_cert_pair->cert_chain};
    cpp_pem_key_cert_pair_list.push_back(::std::move(p));
  }
  **/
  // TODO: add begin() and end() to InlinedVector so above for loop works
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
      ::std::move(gpr_strdup(config->pem_root_certs())),
      ::std::move(cpp_pem_key_cert_pair_list));
  return cpp_config;
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
  key_materials_config_ = ::std::move(key_materials_config);
}

void TlsCredentialReloadArg::set_status(
    grpc_ssl_certificate_config_reload_status status) {
  status_ = status;
}

void TlsCredentialReloadArg::set_error_details(::grpc::string error_details) {
  error_details_ = ::std::move(error_details);
}

/** Creates a smart pointer to a C++ version of the credential reload argument,
 * with the callback function set to a nullptr. **/
::std::unique_ptr<TlsCredentialReloadArg> tls_credential_reload_arg_c_to_cpp(
    const grpc_tls_credential_reload_arg* arg) {
  ::std::unique_ptr<TlsCredentialReloadArg> cpp_arg(
      new TlsCredentialReloadArg());
  cpp_arg->set_cb(nullptr);
  cpp_arg->set_cb_user_data(arg->cb_user_data);
  cpp_arg->set_key_materials_config(
      cpp_key_materials(arg->key_materials_config));
  cpp_arg->set_status(arg->status);
  cpp_arg->set_error_details(arg->error_details);
  return cpp_arg;
}

grpc_tls_on_credential_reload_done_cb TlsCredentialReloadArg::c_callback()
    const {
  grpcpp_tls_on_credential_reload_done_cb cpp_cb = cb_;
  std::function<void(grpc_tls_credential_reload_arg*)> c_cb =
      [cpp_cb](grpc_tls_credential_reload_arg* arg) {
        return cpp_cb(tls_credential_reload_arg_c_to_cpp(arg).get());
      };
  return *(c_cb.target<grpc_tls_on_credential_reload_done_cb>());
}

grpc_tls_credential_reload_arg*
TlsCredentialReloadArg::c_credential_reload_arg() const {
  grpc_tls_credential_reload_arg* c_arg = new grpc_tls_credential_reload_arg();
  c_arg->cb = this->c_callback();
  c_arg->cb_user_data = cb_user_data_;
  c_arg->key_materials_config = key_materials_config_->c_key_materials();
  c_arg->status = status_;
  c_arg->error_details = gpr_strdup(error_details_.c_str());
  return c_arg;
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
  typedef int (*grpcpp_tls_credential_reload_schedule)(
      void* config_user_data, TlsCredentialReloadArg* arg);
  grpcpp_tls_credential_reload_schedule cpp_schedule = schedule_;
  typedef int (*grpc_tls_credential_reload_schedule)(
      void* config_user_data, grpc_tls_credential_reload_arg* arg);
  std::function<int(void*, grpc_tls_credential_reload_arg*)> c_schedule =
      [cpp_schedule](void* config_user_data,
                     grpc_tls_credential_reload_arg* arg) {
        return cpp_schedule(config_user_data,
                            tls_credential_reload_arg_c_to_cpp(arg).get());
      };

  typedef void (*grpcpp_tls_credential_reload_cancel)(
      void* config_user_data, TlsCredentialReloadArg* arg);
  grpcpp_tls_credential_reload_cancel cpp_cancel = cancel_;
  typedef void (*grpc_tls_credential_reload_cancel)(
      void* config_user_data, grpc_tls_credential_reload_arg* arg);
  std::function<void(void*, grpc_tls_credential_reload_arg*)> c_cancel =
      [cpp_cancel](void* config_user_data,
                   grpc_tls_credential_reload_arg* arg) {
        return cpp_cancel(config_user_data,
                          tls_credential_reload_arg_c_to_cpp(arg).get());
      };

  grpc_tls_credential_reload_config* c_config =
      grpc_tls_credential_reload_config_create(
          const_cast<void*>(config_user_data_),
          *(c_schedule.target<grpc_tls_credential_reload_schedule>()),
          *(c_cancel.target<grpc_tls_credential_reload_cancel>()), destruct_);
  return c_config;
}

/** gRPC TLS server authorization check arg API implementation **/

/** Creates a smart pointer to a C++ version of the credential reload argument,
 * with the callback function set to a nullptr. **/
::std::unique_ptr<TlsServerAuthorizationCheckArg>
tls_server_authorization_check_arg_c_to_cpp(
    const grpc_tls_server_authorization_check_arg* arg) {
  ::std::unique_ptr<TlsServerAuthorizationCheckArg> cpp_arg(
      new TlsServerAuthorizationCheckArg());
  cpp_arg->set_cb(nullptr);
  cpp_arg->set_cb_user_data(arg->cb_user_data);
  cpp_arg->set_success(arg->success);
  cpp_arg->set_target_name(arg->target_name);
  cpp_arg->set_peer_cert(arg->peer_cert);
  cpp_arg->set_status(arg->status);
  cpp_arg->set_error_details(arg->error_details);
  return cpp_arg;
}

grpc_tls_on_server_authorization_check_done_cb
TlsServerAuthorizationCheckArg::c_callback() const {
  grpcpp_tls_on_server_authorization_check_done_cb cpp_cb = cb_;
  std::function<void(grpc_tls_server_authorization_check_arg*)> c_cb =
      [cpp_cb](grpc_tls_server_authorization_check_arg* arg) {
        return cpp_cb(tls_server_authorization_check_arg_c_to_cpp(arg).get());
      };
  return *(c_cb.target<grpc_tls_on_server_authorization_check_done_cb>());
}

grpc_tls_server_authorization_check_arg*
TlsServerAuthorizationCheckArg::c_server_authorization_check_arg() const {
  grpc_tls_server_authorization_check_arg* c_arg =
      new grpc_tls_server_authorization_check_arg();
  c_arg->cb = this->c_callback();
  c_arg->cb_user_data = cb_user_data_;
  c_arg->success = success_;
  c_arg->target_name = gpr_strdup(target_name_.c_str());
  c_arg->peer_cert = gpr_strdup(peer_cert_.c_str());
  c_arg->status = status_;
  c_arg->error_details = gpr_strdup(error_details_.c_str());
  return c_arg;
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
  typedef int (*grpcpp_tls_server_authorization_check_schedule)(
      void* config_user_data, TlsServerAuthorizationCheckArg* arg);
  grpcpp_tls_server_authorization_check_schedule cpp_schedule = schedule_;
  typedef int (*grpc_tls_server_authorization_check_schedule)(
      void* config_user_data, grpc_tls_server_authorization_check_arg* arg);
  std::function<int(void*, grpc_tls_server_authorization_check_arg*)>
      c_schedule =
          [cpp_schedule](void* config_user_data,
                         grpc_tls_server_authorization_check_arg* arg) {
            return cpp_schedule(
                config_user_data,
                tls_server_authorization_check_arg_c_to_cpp(arg).get());
          };
  typedef void (*grpcpp_tls_server_authorization_check_cancel)(
      void* config_user_data, TlsServerAuthorizationCheckArg* arg);
  grpcpp_tls_server_authorization_check_cancel cpp_cancel = cancel_;
  typedef void (*grpc_tls_server_authorization_check_cancel)(
      void* config_user_data, grpc_tls_server_authorization_check_arg* arg);
  std::function<void(void*, grpc_tls_server_authorization_check_arg*)>
      c_cancel = [cpp_cancel](void* config_user_data,
                              grpc_tls_server_authorization_check_arg* arg) {
        return cpp_cancel(
            config_user_data,
            tls_server_authorization_check_arg_c_to_cpp(arg).get());
      };
  grpc_tls_server_authorization_check_config* c_config =
      grpc_tls_server_authorization_check_config_create(
          const_cast<void*>(config_user_data_),
          *(c_schedule.target<grpc_tls_server_authorization_check_schedule>()),
          *(c_cancel.target<grpc_tls_server_authorization_check_cancel>()),
          destruct_);
  return c_config;
}

/** gRPC TLS credential options API implementation **/
grpc_tls_credentials_options* TlsCredentialsOptions::c_credentials_options()
    const {
  grpc_tls_credentials_options* c_options =
      grpc_tls_credentials_options_create();
  c_options->set_cert_request_type(cert_request_type_);
  c_options->set_key_materials_config(
      ::grpc_core::RefCountedPtr<grpc_tls_key_materials_config>(
          key_materials_config_->c_key_materials()));
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
