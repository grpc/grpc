//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/lib/security/credentials/tls/tls_credentials.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"
#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"
#include "src/core/util/useful.h"

namespace {

bool CredentialOptionSanityCheck(grpc_tls_credentials_options* options,
                                 bool is_client) {
  if (options == nullptr) {
    LOG(ERROR) << "TLS credentials options is nullptr.";
    return false;
  }
  // In this case, there will be non-retriable handshake errors.
  if (options->min_tls_version() > options->max_tls_version()) {
    LOG(ERROR) << "TLS min version must not be higher than max version.";
    grpc_tls_credentials_options_destroy(options);
    return false;
  }
  if (options->max_tls_version() > grpc_tls_version::TLS1_3) {
    LOG(ERROR) << "TLS max version must not be higher than v1.3.";
    grpc_tls_credentials_options_destroy(options);
    return false;
  }
  if (options->min_tls_version() < grpc_tls_version::TLS1_2) {
    LOG(ERROR) << "TLS min version must not be lower than v1.2.";
    grpc_tls_credentials_options_destroy(options);
    return false;
  }
  if (!options->crl_directory().empty() && options->crl_provider() != nullptr) {
    LOG(ERROR) << "Setting crl_directory and crl_provider not supported. Using "
                  "the crl_provider.";
    // TODO(gtcooke94) - Maybe return false here. Right now object lifetime of
    // this options struct is leaky if false is returned and represents a more
    // complex fix to handle in another PR.
  }
  // In the following conditions, there won't be any issues, but it might
  // indicate callers are doing something wrong with the API.
  if (is_client && options->cert_request_type() !=
                       GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE) {
    LOG(ERROR)
        << "Client's credentials options should not set cert_request_type.";
  }
  if (!is_client && !options->verify_server_cert()) {
    LOG(ERROR)
        << "Server's credentials options should not set verify_server_cert.";
  }
  // In the following conditions, there could be severe security issues.
  if (is_client && options->certificate_verifier() == nullptr) {
    // If no verifier is specified on the client side, use the hostname verifier
    // as default. Users who want to bypass all the verifier check should
    // implement an external verifier instead.
    VLOG(2) << "No verifier specified on the client side. Using default "
               "hostname verifier";
    options->set_certificate_verifier(
        grpc_core::MakeRefCounted<grpc_core::HostNameCertificateVerifier>());
  }
  return true;
}

}  // namespace

TlsCredentials::TlsCredentials(
    grpc_core::RefCountedPtr<grpc_tls_credentials_options> options)
    : options_(std::move(options)) {}

TlsCredentials::~TlsCredentials() {}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
TlsCredentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target_name, grpc_core::ChannelArgs* args) {
  absl::optional<std::string> overridden_target_name =
      args->GetOwnedString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
  auto* ssl_session_cache = args->GetObject<tsi::SslSessionLRUCache>();
  grpc_core::RefCountedPtr<grpc_channel_security_connector> sc =
      grpc_core::TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
          this->Ref(), options_, std::move(call_creds), target_name,
          overridden_target_name.has_value() ? overridden_target_name->c_str()
                                             : nullptr,
          ssl_session_cache == nullptr ? nullptr : ssl_session_cache->c_ptr());
  if (sc == nullptr) {
    return nullptr;
  }
  *args = args->Set(GRPC_ARG_HTTP2_SCHEME, "https");
  return sc;
}

grpc_core::UniqueTypeName TlsCredentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Tls");
  return kFactory.Create();
}

int TlsCredentials::cmp_impl(const grpc_channel_credentials* other) const {
  const TlsCredentials* o = static_cast<const TlsCredentials*>(other);
  if (*options_ == *o->options_) return 0;
  return grpc_core::QsortCompare(
      static_cast<const grpc_channel_credentials*>(this), other);
}

TlsServerCredentials::TlsServerCredentials(
    grpc_core::RefCountedPtr<grpc_tls_credentials_options> options)
    : options_(std::move(options)) {}

TlsServerCredentials::~TlsServerCredentials() {}

grpc_core::RefCountedPtr<grpc_server_security_connector>
TlsServerCredentials::create_security_connector(
    const grpc_core::ChannelArgs& /* args */) {
  return grpc_core::TlsServerSecurityConnector::
      CreateTlsServerSecurityConnector(this->Ref(), options_);
}

grpc_core::UniqueTypeName TlsServerCredentials::type() const {
  static grpc_core::UniqueTypeName::Factory kFactory("Tls");
  return kFactory.Create();
}

/// -- Wrapper APIs declared in grpc_security.h -- *

grpc_channel_credentials* grpc_tls_credentials_create(
    grpc_tls_credentials_options* options) {
  if (!CredentialOptionSanityCheck(options, true /* is_client */)) {
    return nullptr;
  }
  return new TlsCredentials(
      grpc_core::RefCountedPtr<grpc_tls_credentials_options>(options));
}

grpc_server_credentials* grpc_tls_server_credentials_create(
    grpc_tls_credentials_options* options) {
  if (!CredentialOptionSanityCheck(options, false /* is_client */)) {
    return nullptr;
  }
  return new TlsServerCredentials(
      grpc_core::RefCountedPtr<grpc_tls_credentials_options>(options));
}
