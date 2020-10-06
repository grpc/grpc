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

#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/support/config.h>

#include <memory>
#include <vector>

typedef struct grpc_tls_server_authorization_check_arg
    grpc_tls_server_authorization_check_arg;
typedef struct grpc_tls_server_authorization_check_config
    grpc_tls_server_authorization_check_config;
typedef struct grpc_tls_credentials_options grpc_tls_credentials_options;
typedef struct grpc_tls_certificate_provider grpc_tls_certificate_provider;

namespace grpc {
namespace experimental {

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
  TlsServerAuthorizationCheckArg(grpc_tls_server_authorization_check_arg* arg);
  ~TlsServerAuthorizationCheckArg();

  /** Getters for member fields. **/
  void* cb_user_data() const;
  int success() const;
  std::string target_name() const;
  std::string peer_cert() const;
  std::string peer_cert_full_chain() const;
  grpc_status_code status() const;
  std::string error_details() const;

  /** Setters for member fields. **/
  void set_cb_user_data(void* cb_user_data);
  void set_success(int success);
  void set_target_name(const std::string& target_name);
  void set_peer_cert(const std::string& peer_cert);
  void set_peer_cert_full_chain(const std::string& peer_cert_full_chain);
  void set_status(grpc_status_code status);
  void set_error_details(const std::string& error_details);

  /** Calls the C arg's callback function. **/
  void OnServerAuthorizationCheckDoneCallback();

 private:
  grpc_tls_server_authorization_check_arg* c_arg_;
};

/** An interface that the application derives and uses to instantiate a
 * TlsServerAuthorizationCheckConfig instance. Refer to the definition of the
 * grpc_tls_server_authorization_check_config in grpc_tls_credentials_options.h
 * for more details on the expectations of the member functions of the
 * interface.
 * **/
struct TlsServerAuthorizationCheckInterface {
  virtual ~TlsServerAuthorizationCheckInterface() = default;
  /** A callback that invokes the server authorization check. **/
  virtual int Schedule(TlsServerAuthorizationCheckArg* arg) = 0;
  /** A callback that cancels a server authorization check request. **/
  virtual void Cancel(TlsServerAuthorizationCheckArg* /* arg */) {}
};

/** TLS server authorization check config, wraps
 *  grps_tls_server_authorization_check_config. It is used for experimental
 *  purposes for now and it is subject to change. **/
class TlsServerAuthorizationCheckConfig {
 public:
  TlsServerAuthorizationCheckConfig(
      std::shared_ptr<TlsServerAuthorizationCheckInterface>
          server_authorization_check_interface);
  ~TlsServerAuthorizationCheckConfig();

  int Schedule(TlsServerAuthorizationCheckArg* arg) const {
    if (server_authorization_check_interface_ == nullptr) {
      gpr_log(GPR_ERROR, "server authorization check interface is nullptr");
      if (arg != nullptr) {
        arg->set_status(GRPC_STATUS_NOT_FOUND);
        arg->set_error_details(
            "the interface of the server authorization check config is "
            "nullptr");
      }
      return 1;
    }
    return server_authorization_check_interface_->Schedule(arg);
  }

  void Cancel(TlsServerAuthorizationCheckArg* arg) const {
    if (server_authorization_check_interface_ == nullptr) {
      gpr_log(GPR_ERROR, "server authorization check interface is nullptr");
      if (arg != nullptr) {
        arg->set_status(GRPC_STATUS_NOT_FOUND);
        arg->set_error_details(
            "the interface of the server authorization check config is "
            "nullptr");
      }
      return;
    }
    server_authorization_check_interface_->Cancel(arg);
  }

  /** Returns C struct for the server authorization check config. **/
  grpc_tls_server_authorization_check_config* c_config() const {
    return c_config_;
  }

 private:
  grpc_tls_server_authorization_check_config* c_config_;
  std::shared_ptr<TlsServerAuthorizationCheckInterface>
      server_authorization_check_interface_;
};

// Contains configurable options specified by users to configure their certain
// security features supported in TLS. It is used for experimental purposes for
// now and it is subject to change.
class TlsCredentialsOptions {
 public:
  // Constructor for the client. Using this on the server side will cause
  // undefined behaviors.
  //
  // @param server_verification_option options of whether to choose certain
  // checks, e.g. certificate check, hostname check, etc.
  // @param certificate_provider provider offering TLS credentials that will be
  // used in the TLS handshake.
  // @param authorization_check_config configurations that will perform a custom
  // authorization check besides normal check specified by
  // server_verification_option.
  explicit TlsCredentialsOptions(
      grpc_tls_server_verification_option server_verification_option,
      std::shared_ptr<CertificateProviderInterface> certificate_provider,
      std::shared_ptr<TlsServerAuthorizationCheckConfig>
          authorization_check_config);
  // Constructor for the server. Using this on the client side will cause
  // undefined behaviors.
  //
  // @param cert_request_type options of whether to request and verify client
  // certs.
  // @param certificate_provider provider offering TLS credentials that will be
  // used in the TLS handshake.
  explicit TlsCredentialsOptions(
      grpc_ssl_client_certificate_request_type cert_request_type,
      std::shared_ptr<CertificateProviderInterface> certificate_provider);

  ~TlsCredentialsOptions();

  // Getters for member fields.
  std::shared_ptr<TlsServerAuthorizationCheckConfig>
  server_authorization_check_config() const {
    return server_authorization_check_config_;
  }
  // Question: ideally, users don't need to directly interact with the c struct.
  // To provide better encapsulation, we might want to exposed this only to the
  // places where it is used(secure_credentials.cc,
  // secure_server_credentials.cc) and tests. What's the best way to achieve
  // this?
  grpc_tls_credentials_options* c_credentials_options() const {
    return c_credentials_options_;
  }
  // Watches the updates of root certificates with name |root_cert_name|.
  // If used in TLS credentials, it should always be set unless the root
  // certificates are not needed(e.g. in the one-side TLS scenario, the server
  // is not required to verify the client).
  //
  // @return 1 on success, otherwise 0.
  int watch_root_certs();
  // Sets the name of root certificates being watched, if |watch_root_certs| is
  // called. If not set, an empty string will be used as the name.
  //
  // @param root_cert_name the name of root certs being set.
  // @return 1 on success, otherwise 0.
  int set_root_cert_name(const std::string& root_cert_name);
  // Watches the updates of identity key-cert pairs with name
  // |identity_cert_name|. If used in TLS credentials, it should always be set
  // unless the identity certificates are not needed(e.g. in the one-side TLS
  // scenario, the client is not required to provide certs).
  //
  // @return 1 on success, otherwise 0.
  int watch_identity_key_cert_pairs();
  // Sets the name of identity key-cert pairs being watched, if
  // |watch_identity_key_cert_pairs| is called. If not set, an empty string will
  // be used as the name.
  //
  // @param identity_cert_name the name of identity key-cert pairs being set.
  // @return 1 on success, otherwise 0.
  int set_identity_cert_name(const std::string& identity_cert_name);

 private:
  std::shared_ptr<CertificateProviderInterface> certificate_provider_;
  std::shared_ptr<TlsServerAuthorizationCheckConfig>
      server_authorization_check_config_;
  grpc_tls_credentials_options* c_credentials_options_;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_TLS_CREDENTIALS_OPTIONS_H
