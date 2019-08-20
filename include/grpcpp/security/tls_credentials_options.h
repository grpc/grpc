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
  const int version() const { return version_; }

  /** Setter for key materials that will be called by the user. The setter
   * transfers ownership of the arguments to the config. **/
  void set_key_materials(grpc::string pem_root_certs,
                         ::std::vector<PemKeyCertPair> pem_key_cert_pair_list);
  void set_version(int version) { version_ = version;};

 private:
  int version_;
  ::std::vector<PemKeyCertPair> pem_key_cert_pair_list_;
  grpc::string pem_root_certs_;
};

/** The following 2 functions are exposed for testing purposes. **/
grpc_tls_key_materials_config* c_key_materials(
    const ::std::shared_ptr<TlsKeyMaterialsConfig>& config);

::std::shared_ptr<TlsKeyMaterialsConfig> tls_key_materials_c_to_cpp(
    const grpc_tls_key_materials_config* config);


/** TLS credential reload arguments, wraps grpc_tls_credential_reload_arg. **/
class TlsCredentialReloadArg {
 public:
  TlsCredentialReloadArg();
  TlsCredentialReloadArg(grpc_tls_credential_reload_arg arg);
  ~TlsCredentialReloadArg();

  /** Getters for member fields. The callback function is not exposed. **/
  void* cb_user_data() const;
  ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config() const;
  grpc_ssl_certificate_config_reload_status status() const;
  ::std::shared_ptr<grpc::string> error_details() const;

  /** Setters for member fields. **/
  void set_cb_user_data(void* cb_user_data);
  void set_key_materials_config(
      ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config);
  void set_status(grpc_ssl_certificate_config_reload_status status);
  void set_error_details(const grpc::string& error_details);

  /** Calls the C arg's callback function. **/
  void callback() ;

 private:
  grpc_tls_credential_reload_arg c_arg_;
};

// Exposed for testing purposes.
int tls_credential_reload_config_c_schedule(
    void* config_user_data, grpc_tls_credential_reload_arg* arg);
void tls_credential_reload_config_c_cancel(void* config_user_data,
                                           grpc_tls_credential_reload_arg* arg);

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
  /** Returns a C struct for the credential reload config. **/
  grpc_tls_credential_reload_config* c_credential_reload() const {
    return c_config_;
  }

 private:
  grpc_tls_credential_reload_config* c_config_;
  void* config_user_data_;
  int (*schedule_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*cancel_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*destruct_)(void* config_user_data);
};

/** TLS server authorization check arguments, wraps
 *  grpc_tls_server_authorization_check_arg. **/

class TlsServerAuthorizationCheckArg {
 public:
  TlsServerAuthorizationCheckArg();
  TlsServerAuthorizationCheckArg(grpc_tls_server_authorization_check_arg arg);
  ~TlsServerAuthorizationCheckArg();

  /** Getters for member fields. **/
  void* cb_user_data() const;
  int success() const;
  ::std::shared_ptr<grpc::string> target_name() const;
  ::std::shared_ptr<grpc::string> peer_cert() const;
  grpc_status_code status() const;
  ::std::shared_ptr<grpc::string> error_details() const;

  /** Setters for member fields. **/
  void set_cb_user_data(void* cb_user_data);
  void set_success(int success);
  void set_target_name(const grpc::string& target_name);
  void set_peer_cert(const grpc::string& peer_cert);
  void set_status(grpc_status_code status);
  void set_error_details(const grpc::string& error_details);

  /** Calls the C arg's callback function. **/
  void callback();

 private:
  grpc_tls_server_authorization_check_arg c_arg_;
};

// Exposed for testing purposes.
int tls_server_authorization_check_config_c_schedule(
    void* config_user_data, grpc_tls_server_authorization_check_arg* arg);
void tls_server_authorization_check_config_c_cancel(void* config_user_data,
                                           grpc_tls_server_authorization_check_arg* arg);


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

  /** Creates C struct for the credential reload config. **/
  grpc_tls_server_authorization_check_config* c_server_authorization_check() const {
    return c_config_;
  }

 private:
  grpc_tls_server_authorization_check_arg* c_config_;
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
  ::std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config() const {
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
