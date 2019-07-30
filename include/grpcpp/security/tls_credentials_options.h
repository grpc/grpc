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

#ifndef GRPCPP_TLS_CREDENTIALS_OPTIONS_H
#define GRPCPP_TLS_CREDENTIALS_OPTIONS_H

#include <vector>
#include <memory>

#include <grpcpp/support/config.h>
#include <grpc/grpc_security_constants.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

namespace grpc_impl {
namespace experimental {

/** TLS key materials config, wraps grpc_tls_key_materials_config. **/
class TlsKeyMaterialsConfig {
 public:
  struct PemKeyCertPair {
    ::grpc::string private_key;
    ::grpc::string cert_chain;
  };

  /** Getters for member fields. **/
  const ::grpc::string pem_root_certs() const {
    return pem_root_certs_;
  }
  const ::std::vector<PemKeyCertPair>& pem_key_cert_pair_list() const {
    return pem_key_cert_pair_list_;
  }

  /**Setter for member fields. **/
  void set_key_materials(::grpc::string pem_root_certs,
                         ::std::vector<PemKeyCertPair> pem_key_cert_pair_list);

  /** Creates C struct for key materials. **/
  grpc_core::RefCountedPtr<grpc_tls_key_materials_config> c_key_materials() const;

 private:
  ::std::vector<PemKeyCertPair> pem_key_cert_pair_list_;
  ::grpc::string pem_root_certs_;
};

/** TLS credential reload arguments, wraps grpc_tls_credential_reload_arg. **/
class TlsCredentialReloadArg {
 public:

 private:
};

/** TLS credential reloag config, wraps grpc_tls_credential_reload_config. **/
class TlsCredentialReloadConfig {
 public:
  // should this whole discussion use smart pointers?
  TlsCredentialReloadConfig(
      const void* config_user_data,
      int (*schedule)(void* config_user_data, TlsCredentialReloadArg* arg),
      void (*cancel)(void* config_user_data, TlsCredentialReloadArg* arg),
      void (*destruct)(void* config_user_data));
  ~TlsCredentialReloadConfig();

  int Schedule(TlsCredentialReloadArg* arg) const {
    return schedule_(config_user_data_, arg);
  }

  void Cancel(TlsCredentialReloadArg* arg) const {
    if (cancel_ == nullptr) {
      // log info
    }
    cancel_(config_user_data_, arg);
  }

  grpc_core::RefCountedPtr<grpc_tls_credential_reload_config>
      c_credential_reload() const;

 private:
  void* config_user_data_;
  int (*schedule_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*cancel_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*destruct_)(void* config_user_data);
};

/** TLS server authorization check  arguments, wraps
 * grpc_tls_server_authorization_check_arg. **/
class TlsServerAuthorizationCheckArg {
 public:
 private:
};

/** TLS server authorization check config, wraps
 * grps_tls_server_authorization_check_config. **/
class TlsServerAuthorizationCheckConfig {
 public:
  TlsServerAuthorizationCheckConfig(
      const void* config_user_data,
      int (*schedule)(void* config_user_data, TlsServerAuthorizationCheckArg* arg),
      void (*cancel)(void* config_user_data, TlsServerAuthorizationCheckArg* arg),
      void (*destruct)(void* config_user_data));
  ~TlsServerAuthorizationCheckConfig();

  int Schedule(TlsServerAuthorizationCheckArg* arg) const {
    return schedule_(config_user_data_, arg);
  }

  void Cancel(TlsServerAuthorizationCheckArg* arg) const {
    if (cancel_ == nullptr) {
      // log info
    }
    cancel_(config_user_data_, arg);
  }

  grpc_core::RefCountedPtr<grpc_tls_server_authorization_check_config>
      c_server_authorization_check() const;

 private:
    void* config_user_data_;
    int (*schedule_)(void* config_user_data, TlsServerAuthorizationCheckArg* arg);
    void (*cancel_)(void* config_user_data, TlsServerAuthorizationCheckArg* arg);
    void (*destruct_)(void* config_user_data);
};

/** TLS credentials options, wraps grpc_tls_credentials_options. **/
class TlsCredentialsOptions {
 public:
  /** Getters for member fields. **/
  grpc_ssl_client_certificate_request_type cert_request_type() const{
    return cert_request_type_;
  }
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config() const {
    return key_materials_config_;
  }
  std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config() const {
    return credential_reload_config_;
  }
  std::shared_ptr<TlsServerAuthorizationConfig> server_authorization_check_config() const {
    return server_authorization_check_config_;
  }

  /** Setters for member fields. **/
  void set_cert_request_type(
      const grpc_ssl_client_certificate_request_type type) {
    cert_request_type_ = type;
  }
  void set_key_materials_config(
      std::shared_ptr<TlsKeyMaterialsConfig> config) {
    key_materials_config_ = config;
  }
  void set_credential_reload_config(
      std::shared_ptr<TlsCredentialReloadConfig> config) {
    credential_reload_config_ = config;
  }
  void set_server_authorization_check_config(
      std::shared_ptr<TlsServerAuthorizationCheckConfig> config) {
    server_authorization_check_config_ = config;
  }

  /** Creates C struct for TLS credential options. **/
  grpc_tls_credentials_options* c_credentials_options() const;

 private:
  grpc_ssl_client_certificate_request_type cert_request_type_;
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config_;
  std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config_;
  std::shared_ptr<TlsServerAuthorizationCheckConfig> server_authorization_check_config_;
};

} // namespace experimental
} // namespace grpc_impl
#endif /** GRPCPP_TLS_CREDENTIALS_OPTIONS_H **/

