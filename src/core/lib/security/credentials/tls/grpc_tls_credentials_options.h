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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "absl/container/inlined_vector.h"

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

struct grpc_tls_error_details
    : public grpc_core::RefCounted<grpc_tls_error_details> {
 public:
  grpc_tls_error_details() : error_details_("") {}
  void set_error_details(const char* err_details) {
    error_details_ = err_details;
  }
  const std::string& error_details() { return error_details_; }

 private:
  std::string error_details_;
};

/** TLS server authorization check config. **/
struct grpc_tls_server_authorization_check_config
    : public grpc_core::RefCounted<grpc_tls_server_authorization_check_config> {
 public:
  grpc_tls_server_authorization_check_config(
      const void* config_user_data,
      int (*schedule)(void* config_user_data,
                      grpc_tls_server_authorization_check_arg* arg),
      void (*cancel)(void* config_user_data,
                     grpc_tls_server_authorization_check_arg* arg),
      void (*destruct)(void* config_user_data));
  ~grpc_tls_server_authorization_check_config() override;

  void* context() const { return context_; }

  void set_context(void* context) { context_ = context; }

  int Schedule(grpc_tls_server_authorization_check_arg* arg) const;

  void Cancel(grpc_tls_server_authorization_check_arg* arg) const;

 private:
  /** This is a pointer to the wrapped language implementation of
   * grpc_tls_server_authorization_check_config. It is necessary to implement
   * the C schedule and cancel functions, given the schedule or cancel function
   * in a wrapped language. **/
  void* context_ = nullptr;
  /** config-specific, read-only user data that works for all channels created
     with a Credential using the config. */
  void* config_user_data_;

  /** callback function for invoking server authorization check. The
     implementation of this method has to be non-blocking, but can be performed
     synchronously or asynchronously.
     If processing occurs synchronously, it populates \a arg->result, \a
     arg->status, and \a arg->error_details, and returns zero.
     If processing occurs asynchronously, it returns a non-zero value.
     Application then invokes \a arg->cb when processing is completed. Note that
     \a arg->cb cannot be invoked before \a schedule() returns.
  */
  int (*schedule_)(void* config_user_data,
                   grpc_tls_server_authorization_check_arg* arg);

  /** callback function for canceling a server authorization check request. */
  void (*cancel_)(void* config_user_data,
                  grpc_tls_server_authorization_check_arg* arg);

  /** callback function for cleaning up any data associated with server
     authorization check config. */
  void (*destruct_)(void* config_user_data);
};

// Contains configurable options specified by callers to configure their certain
// security features supported in TLS.
// TODO(ZhenLian): consider making this not ref-counted.
struct grpc_tls_credentials_options
    : public grpc_core::RefCounted<grpc_tls_credentials_options> {
 public:
  ~grpc_tls_credentials_options() override = default;

  // Getters for member fields.
  grpc_ssl_client_certificate_request_type cert_request_type() const {
    return cert_request_type_;
  }
  grpc_tls_server_verification_option server_verification_option() const {
    return server_verification_option_;
  }
  grpc_tls_version min_tls_version() const { return min_tls_version_; }
  grpc_tls_version max_tls_version() const { return max_tls_version_; }
  grpc_tls_server_authorization_check_config*
  server_authorization_check_config() const {
    return server_authorization_check_config_.get();
  }
  // Returns the distributor from provider_ if it is set, nullptr otherwise.
  grpc_tls_certificate_distributor* certificate_distributor() {
    if (provider_ != nullptr) return provider_->distributor().get();
    return nullptr;
  }
  bool watch_root_cert() { return watch_root_cert_; }
  const std::string& root_cert_name() { return root_cert_name_; }
  bool watch_identity_pair() { return watch_identity_pair_; }
  const std::string& identity_cert_name() { return identity_cert_name_; }

  // Setters for member fields.
  void set_cert_request_type(
      const grpc_ssl_client_certificate_request_type type) {
    cert_request_type_ = type;
  }
  void set_server_verification_option(
      const grpc_tls_server_verification_option server_verification_option) {
    server_verification_option_ = server_verification_option;
  }
  void set_min_tls_version(grpc_tls_version min_tls_version) {
    min_tls_version_ = min_tls_version;
  }
  void set_max_tls_version(grpc_tls_version max_tls_version) {
    max_tls_version_ = max_tls_version;
  }
  void set_server_authorization_check_config(
      grpc_core::RefCountedPtr<grpc_tls_server_authorization_check_config>
          config) {
    server_authorization_check_config_ = std::move(config);
  }
  // Sets the provider in the options.
  void set_certificate_provider(
      grpc_core::RefCountedPtr<grpc_tls_certificate_provider> provider) {
    provider_ = std::move(provider);
  }
  // If need to watch the updates of root certificates with name
  // |root_cert_name|. The default value is false. If used in tls_credentials,
  // it should always be set to true unless the root certificates are not
  // needed.
  void set_watch_root_cert(bool watch) { watch_root_cert_ = watch; }
  // Sets the name of root certificates being watched, if |set_watch_root_cert|
  // is called. If not set, an empty string will be used as the name.
  void set_root_cert_name(std::string root_cert_name) {
    root_cert_name_ = std::move(root_cert_name);
  }
  // If need to watch the updates of identity certificates with name
  // |identity_cert_name|.
  // The default value is false.
  // If used in tls_credentials, it should always be set to true
  // unless the identity key-cert pairs are not needed.
  void set_watch_identity_pair(bool watch) { watch_identity_pair_ = watch; }
  // Sets the name of identity key-cert pairs being watched, if
  // |set_watch_identity_pair| is called. If not set, an empty string will
  // be used as the name.
  void set_identity_cert_name(std::string identity_cert_name) {
    identity_cert_name_ = std::move(identity_cert_name);
  }

 private:
  grpc_ssl_client_certificate_request_type cert_request_type_ =
      GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
  grpc_tls_server_verification_option server_verification_option_ =
      GRPC_TLS_SERVER_VERIFICATION;
  grpc_tls_version min_tls_version_ = grpc_tls_version::TLS1_2;
  grpc_tls_version max_tls_version_ = grpc_tls_version::TLS1_3;
  grpc_core::RefCountedPtr<grpc_tls_server_authorization_check_config>
      server_authorization_check_config_;
  grpc_core::RefCountedPtr<grpc_tls_certificate_provider> provider_;
  bool watch_root_cert_ = false;
  std::string root_cert_name_;
  bool watch_identity_pair_ = false;
  std::string identity_cert_name_;
};

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H
