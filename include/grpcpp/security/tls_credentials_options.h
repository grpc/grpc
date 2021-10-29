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

#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/support/config.h>

// TODO(yihuazhang): remove the forward declaration here and include
// <grpc/grpc_security.h> directly once the insecure builds are cleaned up.
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
  explicit TlsServerAuthorizationCheckArg(
      grpc_tls_server_authorization_check_arg* arg);
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
  explicit TlsServerAuthorizationCheckConfig(
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

// Base class of configurable options specified by users to configure their
// certain security features supported in TLS. It is used for experimental
// purposes for now and it is subject to change.
class TlsCredentialsOptions {
 public:
  // Constructor for base class TlsCredentialsOptions.
  //
  // @param certificate_provider the provider which fetches TLS credentials that
  // will be used in the TLS handshake
  TlsCredentialsOptions();
  // ---- Setters for member fields ----
  // Sets the certificate provider used to store root certs and identity certs.
  void set_certificate_provider(
      std::shared_ptr<CertificateProviderInterface> certificate_provider);
  // Watches the updates of root certificates with name |root_cert_name|.
  // If used in TLS credentials, setting this field is optional for both the
  // client side and the server side.
  // If this is not set on the client side, we will use the root certificates
  // stored in the default system location, since client side must provide root
  // certificates in TLS(no matter single-side TLS or mutual TLS).
  // If this is not set on the server side, we will not watch any root
  // certificate updates, and assume no root certificates needed for the server
  // (in the one-side TLS scenario, the server is not required to provide root
  // certs). We don't support default root certs on server side.
  void watch_root_certs();
  // Sets the name of root certificates being watched, if |watch_root_certs| is
  // called. If not set, an empty string will be used as the name.
  //
  // @param root_cert_name the name of root certs being set.
  void set_root_cert_name(const std::string& root_cert_name);
  // Watches the updates of identity key-cert pairs with name
  // |identity_cert_name|. If used in TLS credentials, it is required to be set
  // on the server side, and optional for the client side(in the one-side
  // TLS scenario, the client is not required to provide identity certs).
  void watch_identity_key_cert_pairs();
  // Sets the name of identity key-cert pairs being watched, if
  // |watch_identity_key_cert_pairs| is called. If not set, an empty string will
  // be used as the name.
  //
  // @param identity_cert_name the name of identity key-cert pairs being set.
  void set_identity_cert_name(const std::string& identity_cert_name);

  // ----- Getters for member fields ----
  // Get the internal c options. This function shall be used only internally.
  grpc_tls_credentials_options* c_credentials_options() const {
    return c_credentials_options_;
  }

 private:
  std::shared_ptr<CertificateProviderInterface> certificate_provider_;
  grpc_tls_credentials_options* c_credentials_options_ = nullptr;
};

// Contains configurable options on the client side.
// Client side doesn't need to always use certificate provider. When the
// certificate provider is not set, we will use the root certificates stored
// in the system default locations, and assume client won't provide any
// identity certificates(single side TLS).
// It is used for experimental purposes for now and it is subject to change.
class TlsChannelCredentialsOptions final : public TlsCredentialsOptions {
 public:
  // Sets the option to verify the server.
  // The default is GRPC_TLS_SERVER_VERIFICATION.
  void set_server_verification_option(
      grpc_tls_server_verification_option server_verification_option);
  // Sets the custom authorization config.
  void set_server_authorization_check_config(
      std::shared_ptr<TlsServerAuthorizationCheckConfig>
          authorization_check_config);

 private:
};

// Contains configurable options on the server side.
// It is used for experimental purposes for now and it is subject to change.
class TlsServerCredentialsOptions final : public TlsCredentialsOptions {
 public:
  // Server side is required to use a provider, because server always needs to
  // use identity certs.
  explicit TlsServerCredentialsOptions(
      std::shared_ptr<CertificateProviderInterface> certificate_provider)
      : TlsCredentialsOptions() {
    set_certificate_provider(certificate_provider);
  }

  // Sets option to request the certificates from the client.
  // The default is GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE.
  void set_cert_request_type(
      grpc_ssl_client_certificate_request_type cert_request_type);

 private:
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_TLS_CREDENTIALS_OPTIONS_H
