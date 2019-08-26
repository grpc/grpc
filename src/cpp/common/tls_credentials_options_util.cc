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

#include "src/cpp/common/tls_credentials_options_util.h"
#include <grpcpp/security/tls_credentials_options.h>

namespace grpc_impl {
namespace experimental {

/** Creates a new C struct for the key materials. Note that the user must free
 * the underlying pointer to private key and cert chain duplicates; they are not
 * freed when the UniquePtr<char> member variables of PemKeyCertPair are unused.
 * Similarly, the user must free the underlying pointer to c_pem_root_certs. **/
grpc_tls_key_materials_config* ConvertToCKeyMaterialsConfig(
    const std::shared_ptr<TlsKeyMaterialsConfig>& config) {
  grpc_tls_key_materials_config* c_config =
      grpc_tls_key_materials_config_create();
  ::grpc_core::InlinedVector<::grpc_core::PemKeyCertPair, 1>
      c_pem_key_cert_pair_list;
  for (auto key_cert_pair = config->pem_key_cert_pair_list().begin();
       key_cert_pair != config->pem_key_cert_pair_list().end();
       key_cert_pair++) {
    grpc_ssl_pem_key_cert_pair* ssl_pair =
        (grpc_ssl_pem_key_cert_pair*)gpr_malloc(
            sizeof(grpc_ssl_pem_key_cert_pair));
    ssl_pair->private_key = gpr_strdup(key_cert_pair->private_key.c_str());
    ssl_pair->cert_chain = gpr_strdup(key_cert_pair->cert_chain.c_str());
    ::grpc_core::PemKeyCertPair c_pem_key_cert_pair =
        ::grpc_core::PemKeyCertPair(ssl_pair);
    c_pem_key_cert_pair_list.push_back(::std::move(c_pem_key_cert_pair));
  }
  ::grpc_core::UniquePtr<char> c_pem_root_certs(
      gpr_strdup(config->pem_root_certs().c_str()));
  c_config->set_key_materials(std::move(c_pem_root_certs),
                              std::move(c_pem_key_cert_pair_list));
  c_config->set_version(config->version());
  return c_config;
}

/** Creates a new TlsKeyMaterialsConfig from a C struct config. **/
std::shared_ptr<TlsKeyMaterialsConfig> ConvertToCppKeyMaterialsConfig(
    const grpc_tls_key_materials_config* config) {
  std::shared_ptr<TlsKeyMaterialsConfig> cpp_config(
      new TlsKeyMaterialsConfig());
  std::vector<TlsKeyMaterialsConfig::PemKeyCertPair> cpp_pem_key_cert_pair_list;
  grpc_tls_key_materials_config::PemKeyCertPairList pem_key_cert_pair_list =
      config->pem_key_cert_pair_list();
  for (size_t i = 0; i < pem_key_cert_pair_list.size(); i++) {
    ::grpc_core::PemKeyCertPair key_cert_pair = pem_key_cert_pair_list[i];
    TlsKeyMaterialsConfig::PemKeyCertPair p = {
        // gpr_strdup(key_cert_pair.private_key()),
        // gpr_strdup(key_cert_pair.cert_chain())};
        key_cert_pair.private_key(), key_cert_pair.cert_chain()};
    cpp_pem_key_cert_pair_list.push_back(::std::move(p));
  }
  cpp_config->set_key_materials(std::move(config->pem_root_certs()),
                                std::move(cpp_pem_key_cert_pair_list));
  cpp_config->set_version(config->version());
  return cpp_config;
}

/** The C schedule and cancel functions for the credential reload config. **/
int TlsCredentialReloadConfigCSchedule(
    void* config_user_data, grpc_tls_credential_reload_arg* arg) {
  TlsCredentialReloadConfig* cpp_config =
      static_cast<TlsCredentialReloadConfig*>(arg->config->context());
  TlsCredentialReloadArg cpp_arg(*arg);
  int schedule_output = cpp_config->Schedule(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->key_materials_config =
      ConvertToCKeyMaterialsConfig(cpp_arg.key_materials_config());
  arg->status = cpp_arg.status();
  arg->error_details = gpr_strdup(cpp_arg.error_details().c_str());
  return schedule_output;
}

void TlsCredentialReloadConfigCCancel(
    void* config_user_data, grpc_tls_credential_reload_arg* arg) {
  TlsCredentialReloadConfig* cpp_config =
      static_cast<TlsCredentialReloadConfig*>(arg->config->context());
  TlsCredentialReloadArg cpp_arg(*arg);
  cpp_config->Cancel(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->key_materials_config =
      ConvertToCKeyMaterialsConfig(cpp_arg.key_materials_config());
  arg->status = cpp_arg.status();
  arg->error_details = gpr_strdup(cpp_arg.error_details().c_str());
}

/** The C schedule and cancel functions for the server authorization check
 * config. **/
int TlsServerAuthorizationCheckConfigCSchedule(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg) {
  TlsServerAuthorizationCheckConfig* cpp_config =
      static_cast<TlsServerAuthorizationCheckConfig*>(arg->config->context());
  TlsServerAuthorizationCheckArg cpp_arg(*arg);
  int schedule_output = cpp_config->Schedule(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->success = cpp_arg.success();
  arg->target_name = gpr_strdup(cpp_arg.target_name().c_str());
  arg->peer_cert = gpr_strdup(cpp_arg.peer_cert().c_str());
  arg->status = cpp_arg.status();
  arg->error_details = gpr_strdup(cpp_arg.error_details().c_str());
  return schedule_output;
}

void TlsServerAuthorizationCheckConfigCCancel(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg) {
  TlsServerAuthorizationCheckConfig* cpp_config =
      static_cast<TlsServerAuthorizationCheckConfig*>(arg->config->context());
  TlsServerAuthorizationCheckArg cpp_arg(*arg);
  cpp_config->Cancel(&cpp_arg);
  arg->cb_user_data = cpp_arg.cb_user_data();
  arg->success = cpp_arg.success();
  arg->target_name = gpr_strdup(cpp_arg.target_name().c_str());
  arg->peer_cert = gpr_strdup(cpp_arg.peer_cert().c_str());
  arg->status = cpp_arg.status();
  arg->error_details = gpr_strdup(cpp_arg.error_details().c_str());
}

}  // namespace experimental
}  // namespace grpc_impl
