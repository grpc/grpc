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

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpcpp/security/tls_credentials_options.h>

#include "absl/container/inlined_vector.h"
// TODO(ZhenLian): clean up the server authorization part and remove this.
// Only import grpc_security.h.
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/cpp/common/tls_credentials_options_util.h"

namespace grpc {
namespace experimental {

/** gRPC TLS server authorization check arg API implementation **/
TlsServerAuthorizationCheckArg::TlsServerAuthorizationCheckArg(
    grpc_tls_server_authorization_check_arg* arg)
    : c_arg_(arg) {
  GPR_ASSERT(c_arg_ != nullptr);
  if (c_arg_->context != nullptr) {
    gpr_log(GPR_ERROR, "c_arg context has already been set");
  }
  c_arg_->context = static_cast<void*>(this);
  c_arg_->destroy_context = &TlsServerAuthorizationCheckArgDestroyContext;
}

TlsServerAuthorizationCheckArg::~TlsServerAuthorizationCheckArg() {}

void* TlsServerAuthorizationCheckArg::cb_user_data() const {
  return c_arg_->cb_user_data;
}

int TlsServerAuthorizationCheckArg::success() const { return c_arg_->success; }

std::string TlsServerAuthorizationCheckArg::target_name() const {
  std::string cpp_target_name(c_arg_->target_name);
  return cpp_target_name;
}

std::string TlsServerAuthorizationCheckArg::peer_cert() const {
  std::string cpp_peer_cert(c_arg_->peer_cert);
  return cpp_peer_cert;
}

std::string TlsServerAuthorizationCheckArg::peer_cert_full_chain() const {
  std::string cpp_peer_cert_full_chain(c_arg_->peer_cert_full_chain);
  return cpp_peer_cert_full_chain;
}

grpc_status_code TlsServerAuthorizationCheckArg::status() const {
  return c_arg_->status;
}

std::string TlsServerAuthorizationCheckArg::error_details() const {
  return c_arg_->error_details->error_details();
}

void TlsServerAuthorizationCheckArg::set_cb_user_data(void* cb_user_data) {
  c_arg_->cb_user_data = cb_user_data;
}

void TlsServerAuthorizationCheckArg::set_success(int success) {
  c_arg_->success = success;
}

void TlsServerAuthorizationCheckArg::set_target_name(
    const std::string& target_name) {
  c_arg_->target_name = gpr_strdup(target_name.c_str());
}

void TlsServerAuthorizationCheckArg::set_peer_cert(
    const std::string& peer_cert) {
  c_arg_->peer_cert = gpr_strdup(peer_cert.c_str());
}

void TlsServerAuthorizationCheckArg::set_peer_cert_full_chain(
    const std::string& peer_cert_full_chain) {
  c_arg_->peer_cert_full_chain = gpr_strdup(peer_cert_full_chain.c_str());
}

void TlsServerAuthorizationCheckArg::set_status(grpc_status_code status) {
  c_arg_->status = status;
}

void TlsServerAuthorizationCheckArg::set_error_details(
    const std::string& error_details) {
  c_arg_->error_details->set_error_details(error_details.c_str());
}

void TlsServerAuthorizationCheckArg::OnServerAuthorizationCheckDoneCallback() {
  if (c_arg_->cb == nullptr) {
    gpr_log(GPR_ERROR, "server authorizaton check arg callback API is nullptr");
    return;
  }
  c_arg_->cb(c_arg_);
}

TlsServerAuthorizationCheckConfig::TlsServerAuthorizationCheckConfig(
    std::shared_ptr<TlsServerAuthorizationCheckInterface>
        server_authorization_check_interface)
    : server_authorization_check_interface_(
          std::move(server_authorization_check_interface)) {
  c_config_ = grpc_tls_server_authorization_check_config_create(
      nullptr, &TlsServerAuthorizationCheckConfigCSchedule,
      &TlsServerAuthorizationCheckConfigCCancel, nullptr);
  c_config_->set_context(static_cast<void*>(this));
}

TlsServerAuthorizationCheckConfig::~TlsServerAuthorizationCheckConfig() {
  if (c_config_ != nullptr) {
    grpc_tls_server_authorization_check_config_release(c_config_);
  }
}

TlsCredentialsOptions::TlsCredentialsOptions(
    grpc_tls_server_verification_option server_verification_option,
    std::shared_ptr<CertificateProviderInterface> certificate_provider,
    std::shared_ptr<TlsServerAuthorizationCheckConfig>
        authorization_check_config)
    : certificate_provider_(std::move(certificate_provider)),
      server_authorization_check_config_(
          std::move(authorization_check_config)) {
  c_credentials_options_ = grpc_tls_credentials_options_create();
  grpc_tls_credentials_options_set_server_verification_option(
      c_credentials_options_, server_verification_option);
  if (certificate_provider_ != nullptr) {
    grpc_tls_credentials_options_set_certificate_provider(
        c_credentials_options_, certificate_provider_->c_provider());
  }
  if (server_authorization_check_config_ != nullptr) {
    grpc_tls_credentials_options_set_server_authorization_check_config(
        c_credentials_options_, server_authorization_check_config_->c_config());
  }
}

TlsCredentialsOptions::TlsCredentialsOptions(
    grpc_ssl_client_certificate_request_type cert_request_type,
    std::shared_ptr<CertificateProviderInterface> certificate_provider)
    : certificate_provider_(std::move(certificate_provider)) {
  c_credentials_options_ = grpc_tls_credentials_options_create();
  grpc_tls_credentials_options_set_cert_request_type(c_credentials_options_,
                                                     cert_request_type);
  if (certificate_provider_ != nullptr) {
    grpc_tls_credentials_options_set_certificate_provider(
        c_credentials_options_, certificate_provider_->c_provider());
  }
}

int TlsCredentialsOptions::watch_root_certs() {
  return grpc_tls_credentials_options_watch_root_certs(c_credentials_options_);
}

int TlsCredentialsOptions::set_root_cert_name(
    const std::string& root_cert_name) {
  return grpc_tls_credentials_options_set_root_cert_name(
      c_credentials_options_, root_cert_name.c_str());
}

int TlsCredentialsOptions::watch_identity_key_cert_pairs() {
  return grpc_tls_credentials_options_watch_identity_key_cert_pairs(
      c_credentials_options_);
}

int TlsCredentialsOptions::set_identity_cert_name(
    const std::string& identity_cert_name) {
  return grpc_tls_credentials_options_set_identity_cert_name(
      c_credentials_options_, identity_cert_name.c_str());
}

}  // namespace experimental
}  // namespace grpc
