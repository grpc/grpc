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

#ifndef GRPCPP_SECURITY_TLS_CREDENTIALS_OPTIONS_H
#define GRPCPP_SECURITY_TLS_CREDENTIALS_OPTIONS_H

#include <functional>
#include <memory>
#include <vector>

#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpcpp/support/config.h>

namespace grpc_impl {
namespace experimental {

/** TLS key materials config, wrapper for grpc_tls_key_materials_config. **/
class TlsKeyMaterialsConfig {
 public:
  struct PemKeyCertPair {
    grpc::string private_key;
    grpc::string cert_chain;
  };

  /** Getters for member fields. **/
  const grpc::string pem_root_certs() const { return pem_root_certs_; }
  const ::std::vector<PemKeyCertPair>& pem_key_cert_pair_list() const {
    return pem_key_cert_pair_list_;
  }

  /**Setter for member fields. **/
  void set_key_materials(grpc::string pem_root_certs,
                         ::std::vector<PemKeyCertPair> pem_key_cert_pair_list);

  /** Creates C struct for key materials. **/
  grpc_tls_key_materials_config* c_key_materials() const;

 private:
  ::std::vector<PemKeyCertPair> pem_key_cert_pair_list_;
  grpc::string pem_root_certs_;
};

/** Creates smart pointer to a C++ version of the C key materials. **/
::std::shared_ptr<TlsKeyMaterialsConfig> tls_key_materials_c_to_cpp(
    const grpc_tls_key_materials_config* config);

/** TLS credential reload arguments, wraps grpc_tls_credential_reload_arg. **/
typedef class TlsCredentialReloadArg TlsCredentialReloadArg;

typedef void (*grpcpp_tls_on_credential_reload_done_cb)(
    TlsCredentialReloadArg* arg);

class TlsCredentialReloadArg {
 public:
  /** Getters for member fields. **/
  grpcpp_tls_on_credential_reload_done_cb cb() const { return cb_; }
  void* cb_user_data() const { return cb_user_data_; }
  ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config() const {
    return key_materials_config_;
  }
  grpc_ssl_certificate_config_reload_status status() const { return status_; }
  grpc::string error_details() const { return error_details_; }

  /** Setters for member fields. **/
  void set_cb(grpcpp_tls_on_credential_reload_done_cb cb);
  void set_cb_user_data(void* cb_user_data);
  void set_key_materials_config(
      ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config);
  void set_status(grpc_ssl_certificate_config_reload_status status);
  void set_error_details(grpc::string error_details);

  /** Creates C struct for credential reload arg. **/
  grpc_tls_credential_reload_arg* c_credential_reload_arg() const;

  /** Creates C callback function from C++ callback function. **/
  grpc_tls_on_credential_reload_done_cb c_callback() const;

 private:
  grpcpp_tls_on_credential_reload_done_cb cb_;
  void* cb_user_data_;
  ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config_;
  grpc_ssl_certificate_config_reload_status status_;
  grpc::string error_details_;
};

/** Creates a smart pointer to a C++ version of the credential reload argument,
 * with the callback function set to a nullptr. **/
::std::unique_ptr<TlsCredentialReloadArg> tls_credential_reload_arg_c_to_cpp(
    const grpc_tls_credential_reload_arg* arg);

/** TLS credential reloag config, wraps grpc_tls_credential_reload_config. **/
class TlsCredentialReloadConfig {
 public:
  TlsCredentialReloadConfig(const void* config_user_data,
                            int (*schedule)(void* config_user_data,
                                            TlsCredentialReloadArg* arg),
                            void (*cancel)(void* config_user_data,
                                           TlsCredentialReloadArg* arg),
                            void (*destruct)(void* config_user_data));
  ~TlsCredentialReloadConfig();

  int Schedule(TlsCredentialReloadArg* arg) const {
    return schedule_(config_user_data_, arg);
  }

  void Cancel(TlsCredentialReloadArg* arg) const {
    if (cancel_ == nullptr) {
      gpr_log(GPR_ERROR, "cancel API is nullptr");
      return;
    }
    cancel_(config_user_data_, arg);
  }
  /** Creates C struct for the credential reload config. **/
  grpc_tls_credential_reload_config* c_credential_reload() const;

 private:
  void* config_user_data_;
  int (*schedule_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*cancel_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*destruct_)(void* config_user_data);
};

/** TLS server authorization check arguments, wraps
 *  grpc_tls_server_authorization_check_arg. **/
typedef class TlsServerAuthorizationCheckArg TlsServerAuthorizationCheckArg;

typedef void (*grpcpp_tls_on_server_authorization_check_done_cb)(
    TlsServerAuthorizationCheckArg* arg);

class TlsServerAuthorizationCheckArg {
 public:
  /** Getters for member fields. **/
  grpcpp_tls_on_server_authorization_check_done_cb cb() const { return cb_; }
  void* cb_user_data() const { return cb_user_data_; }
  int success() const { return success_; }
  grpc::string target_name() const { return target_name_; }
  grpc::string peer_cert() const { return peer_cert_; }
  grpc_status_code status() const { return status_; }
  grpc::string error_details() const { return error_details_; }

  /** Setters for member fields. **/
  void set_cb(grpcpp_tls_on_server_authorization_check_done_cb cb) { cb_ = cb; }
  void set_cb_user_data(void* cb_user_data) { cb_user_data_ = cb_user_data; }
  void set_success(int success) { success_ = success; };
  void set_target_name(grpc::string target_name) { target_name_ = target_name; }
  void set_peer_cert(grpc::string peer_cert) {
    peer_cert_ = ::std::move(peer_cert);
  }
  void set_status(grpc_status_code status) { status_ = status; }
  void set_error_details(grpc::string error_details) {
    error_details_ = ::std::move(error_details);
  }

  /** Creates C struct for server authorization check arg. **/
  grpc_tls_server_authorization_check_arg* c_server_authorization_check_arg()
      const;

  /** Creates C callback function from C++ callback function. **/
  grpc_tls_on_server_authorization_check_done_cb c_callback() const;

 private:
  grpcpp_tls_on_server_authorization_check_done_cb cb_;
  void* cb_user_data_;
  int success_;
  grpc::string target_name_;
  grpc::string peer_cert_;
  grpc_status_code status_;
  grpc::string error_details_;
};

/** Creates a smart pointer to a C++ version of the server authorization check
 * argument, with the callback function set to a nullptr. **/
::std::unique_ptr<TlsServerAuthorizationCheckArg>
tls_server_authorization_check_arg_c_to_cpp(
    const grpc_tls_server_authorization_check_arg* arg);

/** TLS server authorization check config, wraps
 *  grps_tls_server_authorization_check_config. **/
class TlsServerAuthorizationCheckConfig {
 public:
  TlsServerAuthorizationCheckConfig(
      const void* config_user_data,
      int (*schedule)(void* config_user_data,
                      TlsServerAuthorizationCheckArg* arg),
      void (*cancel)(void* config_user_data,
                     TlsServerAuthorizationCheckArg* arg),
      void (*destruct)(void* config_user_data));
  ~TlsServerAuthorizationCheckConfig();

  int Schedule(TlsServerAuthorizationCheckArg* arg) const {
    return schedule_(config_user_data_, arg);
  }

  void Cancel(TlsServerAuthorizationCheckArg* arg) const {
    if (cancel_ == nullptr) {
      gpr_log(GPR_ERROR, "cancel API is nullptr");
      return;
    }
    cancel_(config_user_data_, arg);
  }

  /** Creates C struct for the server authorization check config. **/
  grpc_tls_server_authorization_check_config* c_server_authorization_check()
      const;

 private:
  void* config_user_data_;
  int (*schedule_)(void* config_user_data, TlsServerAuthorizationCheckArg* arg);
  void (*cancel_)(void* config_user_data, TlsServerAuthorizationCheckArg* arg);
  void (*destruct_)(void* config_user_data);
};

/** TLS credentials options, wrapper for grpc_tls_credentials_options. **/
class TlsCredentialsOptions {
 public:
  /** Getters for member fields. **/
  grpc_ssl_client_certificate_request_type cert_request_type() const {
    return cert_request_type_;
  }
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config() const {
    return key_materials_config_;
  }
  ::std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config()
      const {
    return credential_reload_config_;
  }
  ::std::shared_ptr<TlsServerAuthorizationCheckConfig>
  server_authorization_check_config() const {
    return server_authorization_check_config_;
  }

  /** Setters for member fields. **/
  void set_cert_request_type(
      const grpc_ssl_client_certificate_request_type type) {
    cert_request_type_ = type;
  }
  void set_key_materials_config(std::shared_ptr<TlsKeyMaterialsConfig> config) {
    key_materials_config_ = ::std::move(config);
  }
  void set_credential_reload_config(
      ::std::shared_ptr<TlsCredentialReloadConfig> config) {
    credential_reload_config_ = ::std::move(config);
  }
  void set_server_authorization_check_config(
      ::std::shared_ptr<TlsServerAuthorizationCheckConfig> config) {
    server_authorization_check_config_ = ::std::move(config);
  }

  /** Creates C struct for TLS credential options. **/
  grpc_tls_credentials_options* c_credentials_options() const;

 private:
  grpc_ssl_client_certificate_request_type cert_request_type_;
  ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config_;
  ::std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config_;
  ::std::shared_ptr<TlsServerAuthorizationCheckConfig>
      server_authorization_check_config_;
};

}  // namespace experimental
}  // namespace grpc_impl

#endif  // GRPCPP_SECURITY_TLS_CREDENTIALS_OPTIONS_H
