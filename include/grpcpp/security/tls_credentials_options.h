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

/** TLS key materials config, wrapper for grpc_tls_key_materials_config. It is
 * used for experimental purposes for now and subject to change. **/
class TlsKeyMaterialsConfig {
 public:
  struct PemKeyCertPair {
    grpc::string private_key;
    grpc::string cert_chain;
  };

  /** Getters for member fields. **/
  const grpc::string pem_root_certs() const { return pem_root_certs_; }
  const std::vector<PemKeyCertPair>& pem_key_cert_pair_list() const {
    return pem_key_cert_pair_list_;
  }
  int version() const { return version_; }

  /** Setter for key materials that will be called by the user. The setter
   * transfers ownership of the arguments to the config. **/
  void set_key_materials(grpc::string pem_root_certs,
                         std::vector<PemKeyCertPair> pem_key_cert_pair_list);
  void set_version(int version) { version_ = version; };

 private:
  int version_ = 0;
  std::vector<PemKeyCertPair> pem_key_cert_pair_list_;
  grpc::string pem_root_certs_;
};

/** TLS credential reload arguments, wraps grpc_tls_credential_reload_arg. It is
 * used for experimental purposes for now and it is subject to change.
 *
 * The credential reload arg contains all the info necessary to schedule/cancel
 * a credential reload request. The callback function must be called after
 * finishing the schedule operation. See the description of the
 * grpc_tls_credential_reload_arg struct in grpc_security.h for more details.
 * **/
class TlsCredentialReloadArg {
 public:
  /** TlsCredentialReloadArg does not take ownership of the C arg that is passed
   * to the constructor. One must remember to free any memory allocated to the C
   * arg after using the setter functions below. **/
  TlsCredentialReloadArg(grpc_tls_credential_reload_arg* arg) : c_arg_(arg) {}
  ~TlsCredentialReloadArg();

  /** Getters for member fields. The callback function is not exposed.
   * They return the corresponding fields of the underlying C arg. In the case
   * of the key materials config, it creates a new instance of the C++ key
   * materials config from the underlying C grpc_tls_key_materials_config. **/
  void* cb_user_data() const;
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config() const;
  grpc_ssl_certificate_config_reload_status status() const;
  grpc::string error_details() const;

  /** Setters for member fields. They modify the fields of the underlying C arg.
   * The setters for the key_materials_config and the error_details allocate
   * memory when modifying c_arg_, so one must remember to free c_arg_'s
   * original key_materials_config or error_details after using the appropriate
   * setter function.
   * **/
  void set_cb_user_data(void* cb_user_data);
  void set_key_materials_config(
      const std::shared_ptr<TlsKeyMaterialsConfig>& key_materials_config);
  void set_status(grpc_ssl_certificate_config_reload_status status);
  void set_error_details(const grpc::string& error_details);

  /** Calls the C arg's callback function. **/
  void OnCredentialReloadDoneCallback();

 private:
  grpc_tls_credential_reload_arg* c_arg_;
};

/** TLS credential reloag config, wraps grpc_tls_credential_reload_config. It is
 * used for experimental purposes for now and it is subject to change.
 *
 * The config_user_data is read-only user data; schedule is a pointer to an
 * application-provided callback that invokes the credential reload; cancel is a
 * pointer to an application-provided callback that cancels a credential reload
 * request; destruct is a pointer to an application-provided callback that
 * cleans up any data associated to the config. See the description of the
 * grpc_tls_credential_reload_config struct in grpc_security.h. **/
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
    if (schedule_ == nullptr) {
      gpr_log(GPR_ERROR, "schedule API is nullptr");
      return 1;
    }
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
  grpc_tls_credential_reload_config* c_config() const { return c_config_; }

 private:
  grpc_tls_credential_reload_config* c_config_;
  void* config_user_data_;
  int (*schedule_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*cancel_)(void* config_user_data, TlsCredentialReloadArg* arg);
  void (*destruct_)(void* config_user_data);
};

/** TLS server authorization check arguments, wraps
 *  grpc_tls_server_authorization_check_arg. It is used for experimental
 *  purposes for now and it is subject to change.
 *
 *  The server authorization check arg contains all the info necessary to
 *  schedule/cancel a server authorization check request. The callback function
 *  must be called after finishing the schedule operation. See the description
 *  of the grpc_tls_server_authorization_check_arg struct in grpc_security.h for
 *  more details. **/
class TlsServerAuthorizationCheckArg {
 public:
  /** TlsServerAuthorizationCheckArg does not take ownership of the C arg passed
   * to the constructor. One must remember to free any memory allocated to the
   * C arg after using the setter functions below. **/
  TlsServerAuthorizationCheckArg(grpc_tls_server_authorization_check_arg* arg)
      : c_arg_(arg) {}
  ~TlsServerAuthorizationCheckArg();

  /** Getters for member fields. They return the corresponding fields of the
   * underlying C arg.**/
  void* cb_user_data() const;
  int success() const;
  grpc::string target_name() const;
  grpc::string peer_cert() const;
  grpc_status_code status() const;
  grpc::string error_details() const;

  /** Setters for member fields. They modify the fields of the underlying C arg.
   * The setters for target_name, peer_cert, and error_details allocate memory
   * when modifying c_arg_, so one must remember to free c_arg_'s original
   * target_name, peer_cert, or error_details after using the appropriate setter
   * function.
   * **/
  void set_cb_user_data(void* cb_user_data);
  void set_success(int success);
  void set_target_name(const grpc::string& target_name);
  void set_peer_cert(const grpc::string& peer_cert);
  void set_status(grpc_status_code status);
  void set_error_details(const grpc::string& error_details);

  /** Calls the C arg's callback function. **/
  void OnServerAuthorizationCheckDoneCallback();

 private:
  grpc_tls_server_authorization_check_arg* c_arg_;
};

/** TLS server authorization check config, wraps
 *  grps_tls_server_authorization_check_config. It is used for experimental
 *  purposes for now and it is subject to change.
 *
 *  The config_user_data is read-only user data; schedule is a pointer to an
 *  application-provided callback that invokes the server authorization check;
 *  cancel is a pointer to an application-provided callback that cancels a
 *  server authorization check request; destruct is a pointer to an
 *  application-provided callback that cleans up any data associated to the
 *  config. See the description of the
 *  grpc_tls_server_authorization_check_config struct in grpc_security.h for
 *  more details. **/
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
    if (schedule_ == nullptr) {
      gpr_log(GPR_ERROR, "schedule API is nullptr");
      return 1;
    }
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
  grpc_tls_server_authorization_check_config* c_config() const {
    return c_config_;
  }

 private:
  grpc_tls_server_authorization_check_config* c_config_;
  void* config_user_data_;
  int (*schedule_)(void* config_user_data, TlsServerAuthorizationCheckArg* arg);
  void (*cancel_)(void* config_user_data, TlsServerAuthorizationCheckArg* arg);
  void (*destruct_)(void* config_user_data);
};

/** TLS credentials options, wrapper for grpc_tls_credentials_options. It is
 * used for experimental purposes for now and it is subject to change. See the
 * description of the grpc_tls_credentials_options struct in grpc_security.h for
 * more details. **/
class TlsCredentialsOptions {
 public:
  TlsCredentialsOptions(
      grpc_ssl_client_certificate_request_type cert_request_type,
      std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config,
      std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config,
      std::shared_ptr<TlsServerAuthorizationCheckConfig>
          server_authorization_check_config);
  ~TlsCredentialsOptions();

  /** Getters for member fields. **/
  grpc_ssl_client_certificate_request_type cert_request_type() const {
    return cert_request_type_;
  }
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config() const {
    return key_materials_config_;
  }
  std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config() const {
    return credential_reload_config_;
  }
  std::shared_ptr<TlsServerAuthorizationCheckConfig>
  server_authorization_check_config() const {
    return server_authorization_check_config_;
  }
  grpc_tls_credentials_options* c_credentials_options() const {
    return c_credentials_options_;
  }

 private:
  grpc_ssl_client_certificate_request_type cert_request_type_;
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config_;
  std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config_;
  std::shared_ptr<TlsServerAuthorizationCheckConfig>
      server_authorization_check_config_;
  grpc_tls_credentials_options* c_credentials_options_;
};

}  // namespace experimental
}  // namespace grpc_impl

#endif  // GRPCPP_SECURITY_TLS_CREDENTIALS_OPTIONS_H
