//
//
// Copyright 2019 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <memory>
#include <string>
#include <utility>

#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/support/log.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>

namespace grpc {
namespace experimental {

TlsCredentialsOptions::TlsCredentialsOptions() {
  c_credentials_options_ = grpc_tls_credentials_options_create();
}

void TlsCredentialsOptions::set_certificate_provider(
    std::shared_ptr<CertificateProviderInterface> certificate_provider) {
  certificate_provider_ = std::move(certificate_provider);
  if (certificate_provider_ != nullptr) {
    grpc_tls_credentials_options_set_certificate_provider(
        c_credentials_options_, certificate_provider_->c_provider());
  }
}

void TlsCredentialsOptions::watch_root_certs() {
  grpc_tls_credentials_options_watch_root_certs(c_credentials_options_);
}

void TlsCredentialsOptions::set_root_cert_name(
    const std::string& root_cert_name) {
  grpc_tls_credentials_options_set_root_cert_name(c_credentials_options_,
                                                  root_cert_name.c_str());
}

void TlsCredentialsOptions::watch_identity_key_cert_pairs() {
  grpc_tls_credentials_options_watch_identity_key_cert_pairs(
      c_credentials_options_);
}

void TlsCredentialsOptions::set_identity_cert_name(
    const std::string& identity_cert_name) {
  grpc_tls_credentials_options_set_identity_cert_name(
      c_credentials_options_, identity_cert_name.c_str());
}

void TlsCredentialsOptions::set_crl_directory(const std::string& path) {
  grpc_tls_credentials_options_set_crl_directory(c_credentials_options_,
                                                 path.c_str());
}

void TlsCredentialsOptions::set_tls_session_key_log_file_path(
    const std::string& tls_session_key_log_file_path) {
  grpc_tls_credentials_options_set_tls_session_key_log_file_path(
      c_credentials_options_, tls_session_key_log_file_path.c_str());
}

void TlsCredentialsOptions::set_certificate_verifier(
    std::shared_ptr<CertificateVerifier> certificate_verifier) {
  certificate_verifier_ = std::move(certificate_verifier);
  if (certificate_verifier_ != nullptr) {
    grpc_tls_credentials_options_set_certificate_verifier(
        c_credentials_options_, certificate_verifier_->c_verifier());
  }
}

void TlsCredentialsOptions::set_check_call_host(bool check_call_host) {
  grpc_tls_credentials_options* options = c_credentials_options();
  GPR_ASSERT(options != nullptr);
  grpc_tls_credentials_options_set_check_call_host(options, check_call_host);
}

void TlsChannelCredentialsOptions::set_verify_server_certs(
    bool verify_server_certs) {
  grpc_tls_credentials_options* options = c_credentials_options();
  GPR_ASSERT(options != nullptr);
  grpc_tls_credentials_options_set_verify_server_cert(options,
                                                      verify_server_certs);
}

void TlsServerCredentialsOptions::set_cert_request_type(
    grpc_ssl_client_certificate_request_type cert_request_type) {
  grpc_tls_credentials_options* options = c_credentials_options();
  GPR_ASSERT(options != nullptr);
  grpc_tls_credentials_options_set_cert_request_type(options,
                                                     cert_request_type);
}

void TlsServerCredentialsOptions::set_client_ca_list(bool set_client_ca_list) {
  grpc_tls_credentials_options* options = c_credentials_options();
  GPR_ASSERT(options != nullptr);
  grpc_tls_credentials_options_set_client_ca_list(options, set_client_ca_list);
}

}  // namespace experimental
}  // namespace grpc
