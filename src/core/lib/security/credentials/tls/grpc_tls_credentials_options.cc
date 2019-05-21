/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

/** -- gRPC TLS key materials config API implementation. -- **/
void grpc_tls_key_materials_config::set_key_materials(
    grpc_core::UniquePtr<char> pem_root_certs,
    PemKeyCertPairList pem_key_cert_pair_list) {
  pem_key_cert_pair_list_ = std::move(pem_key_cert_pair_list);
  pem_root_certs_ = std::move(pem_root_certs);
}

/** -- gRPC TLS credential reload config API implementation. -- **/
grpc_tls_credential_reload_config::grpc_tls_credential_reload_config(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    grpc_tls_credential_reload_arg* arg),
    void (*cancel)(void* config_user_data, grpc_tls_credential_reload_arg* arg),
    void (*destruct)(void* config_user_data))
    : config_user_data_(const_cast<void*>(config_user_data)),
      schedule_(schedule),
      cancel_(cancel),
      destruct_(destruct) {}

grpc_tls_credential_reload_config::~grpc_tls_credential_reload_config() {
  if (destruct_ != nullptr) {
    destruct_((void*)config_user_data_);
  }
}

/** -- gRPC TLS server authorization check API implementation. -- **/
grpc_tls_server_authorization_check_config::
    grpc_tls_server_authorization_check_config(
        const void* config_user_data,
        int (*schedule)(void* config_user_data,
                        grpc_tls_server_authorization_check_arg* arg),
        void (*cancel)(void* config_user_data,
                       grpc_tls_server_authorization_check_arg* arg),
        void (*destruct)(void* config_user_data))
    : config_user_data_(const_cast<void*>(config_user_data)),
      schedule_(schedule),
      cancel_(cancel),
      destruct_(destruct) {}

grpc_tls_server_authorization_check_config::
    ~grpc_tls_server_authorization_check_config() {
  if (destruct_ != nullptr) {
    destruct_((void*)config_user_data_);
  }
}

/** -- Wrapper APIs declared in grpc_security.h -- **/
grpc_tls_credentials_options* grpc_tls_credentials_options_create() {
  return grpc_core::New<grpc_tls_credentials_options>();
}

int grpc_tls_credentials_options_set_cert_request_type(
    grpc_tls_credentials_options* options,
    grpc_ssl_client_certificate_request_type type) {
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_cert_request_type()");
    return 0;
  }
  options->set_cert_request_type(type);
  return 1;
}

int grpc_tls_credentials_options_set_key_materials_config(
    grpc_tls_credentials_options* options,
    grpc_tls_key_materials_config* config) {
  if (options == nullptr || config == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_key_materials_config()");
    return 0;
  }
  options->set_key_materials_config(config->Ref());
  return 1;
}

int grpc_tls_credentials_options_set_credential_reload_config(
    grpc_tls_credentials_options* options,
    grpc_tls_credential_reload_config* config) {
  if (options == nullptr || config == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to "
            "grpc_tls_credentials_options_set_credential_reload_config()");
    return 0;
  }
  options->set_credential_reload_config(config->Ref());
  return 1;
}

int grpc_tls_credentials_options_set_server_authorization_check_config(
    grpc_tls_credentials_options* options,
    grpc_tls_server_authorization_check_config* config) {
  if (options == nullptr || config == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid nullptr arguments to "
        "grpc_tls_credentials_options_set_server_authorization_check_config()");
    return 0;
  }
  options->set_server_authorization_check_config(config->Ref());
  return 1;
}

grpc_tls_key_materials_config* grpc_tls_key_materials_config_create() {
  return grpc_core::New<grpc_tls_key_materials_config>();
}

int grpc_tls_key_materials_config_set_key_materials(
    grpc_tls_key_materials_config* config, const char* root_certs,
    const grpc_ssl_pem_key_cert_pair** key_cert_pairs, size_t num) {
  if (config == nullptr || key_cert_pairs == nullptr || num == 0) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to "
            "grpc_tls_key_materials_config_set_key_materials()");
    return 0;
  }
  grpc_core::UniquePtr<char> pem_root(const_cast<char*>(root_certs));
  grpc_tls_key_materials_config::PemKeyCertPairList cert_pair_list;
  for (size_t i = 0; i < num; i++) {
    grpc_core::PemKeyCertPair key_cert_pair(
        const_cast<grpc_ssl_pem_key_cert_pair*>(key_cert_pairs[i]));
    cert_pair_list.emplace_back(std::move(key_cert_pair));
  }
  config->set_key_materials(std::move(pem_root), std::move(cert_pair_list));
  gpr_free(key_cert_pairs);
  return 1;
}

grpc_tls_credential_reload_config* grpc_tls_credential_reload_config_create(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    grpc_tls_credential_reload_arg* arg),
    void (*cancel)(void* config_user_data, grpc_tls_credential_reload_arg* arg),
    void (*destruct)(void* config_user_data)) {
  if (schedule == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Schedule API is nullptr in creating TLS credential reload config.");
    return nullptr;
  }
  return grpc_core::New<grpc_tls_credential_reload_config>(
      config_user_data, schedule, cancel, destruct);
}

grpc_tls_server_authorization_check_config*
grpc_tls_server_authorization_check_config_create(
    const void* config_user_data,
    int (*schedule)(void* config_user_data,
                    grpc_tls_server_authorization_check_arg* arg),
    void (*cancel)(void* config_user_data,
                   grpc_tls_server_authorization_check_arg* arg),
    void (*destruct)(void* config_user_data)) {
  if (schedule == nullptr) {
    gpr_log(GPR_ERROR,
            "Schedule API is nullptr in creating TLS server authorization "
            "check config.");
    return nullptr;
  }
  return grpc_core::New<grpc_tls_server_authorization_check_config>(
      config_user_data, schedule, cancel, destruct);
}
