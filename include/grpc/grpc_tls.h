//
//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_GRPC_TLS_H
#define GRPC_GRPC_TLS_H

#include <memory>

#include "absl/strings/string_view.h"

#include "grpc.h"
#include "grpc/credentials.h"
#include "grpc/grpc_crl_provider.h"

namespace grpc_core {

// TODO(gtcooke94) create these
class CertificateProviderInterface {};
class CustomChainBuilderInterface {};
class CertificateVerifierInterface {};

// Base class of TLS credentials builder.
class TlsCredentialsBuilder {
 public:
  ~TlsCredentialsBuilder();

  // TODO(gtcooke94) - do we need a copy constructor? How deep does the copy go
  // with regards to things like certificate and crl providers?
  // Copy constructor does a deep copy. No assignment permitted.
  TlsCredentialsBuilder(const TlsCredentialsBuilder& other);

  TlsCredentialsBuilder& operator=(const TlsCredentialsBuilder& other) = delete;

  // ---- Setters for member fields ----
  // Sets the certificate provider which is used to store:
  // 1. The root certificates used to (cryptographically) verify peer
  // certificate chains.
  // 2. The certificate chain conveying the application's identity and the
  // corresponding private key.
  void set_certificate_provider(
      std::shared_ptr<CertificateProviderInterface> certificate_provider);

  // Watches the updates of root certificates with name |name|. If used in TLS
  // credentials, setting this field is optional for both the client side and
  // the server side. If this is not set on the client side, we will use the
  // root certificates stored in the default system location, since client side
  // must provide root certificates in TLS (no matter single-side TLS or mutual
  // TLS). If this is not set on the server side, we will not watch any rootAAA
  // certificate updates, and assume no root certificates are needed for the
  // server (in the one-side TLS scenario, the server is not required to provide
  // root certificates).
  //
  // @param name the name of root certificates being set.
  void watch_root_certificates(absl::string_view name);

  // Watches the updates of identity key-certificate pairs with name
  // |name|. If used in TLS credentials, it is required to be set
  // on the server side, and optional for the client side(in the one-side
  // TLS scenario, the client is not required to provide identity certificates).

  // @param name the name of the identity key cert pairs to watch.
  void watch_identity_key_cert_pairs(absl::string_view name);

  // WARNING: EXPERT USE ONLY. MISUSE CAN LEAD TO SIGNIFICANT SECURITY
  // DEGRADATION.
  //
  // Sets the TLS session key logging file path. If not set, TLS
  // session key logging is disabled. Note that this should be used only for
  // debugging purposes. It should never be used in a production environment
  // due to security concerns - anyone who can obtain the (logged) session key
  // can decrypt all traffic on a connection.
  //
  // @param tls_session_key_log_file_path: Path where TLS session keys should
  // be logged.
  void set_tls_session_key_log_file_path_dangerous(
      absl::string_view tls_session_key_log_file_path);

  // Sets the certificate verifier. The certificate verifier performs checks on
  // the peer certificate chain after the chain has been (cryptographically)
  // verified to chain up to a trusted root.
  // If unset, this will default to the `HostNameCertificateVerifier` detailed
  // below.
  // If set to nulltpr, this will overwrite the host name verifier and lead you
  // to not doing any checks (aside from the cryptographic ones).
  void set_certificate_verifier(
      std::shared_ptr<CertificateVerifierInterface> certificate_verifier);

  // Sets the crl provider, see CrlProvider for more details.
  void set_crl_provider(std::shared_ptr<CrlProvider> crl_provider);

  // Sets the minimum TLS version that will be negotiated during the TLS
  // handshake. If not set, the underlying SSL library will default to TLS v1.2.
  // @param tls_version: The minimum TLS version.
  void set_min_tls_version(grpc_tls_version tls_version);

  // Sets the maximum TLS version that will be negotiated during the TLS
  // handshake. If not set, the underlying SSL library will default to TLS v1.3.
  // @param tls_version: The maximum TLS version.
  void set_max_tls_version(grpc_tls_version tls_version);

  // WARNING: EXPERT USE ONLY. MISUSE CAN LEAD TO SIGNIFICANT SECURITY
  // DEGRADATION.
  //
  // Sets a custom chain builder implementation that replaces the default chain
  // building from the underlying SSL library. Fully replacing and implementing
  // chain building is a complex task and has dangerous security implications if
  // done wrong, thus this API is inteded for expert use only.
  void set_custom_chain_builder_dangerous(
      std::shared_ptr<CustomChainBuilderInterface> chain_builder);

 protected:
  TlsCredentialsBuilder() = default;

 private:
  grpc_tls_version min_tls_version_ = grpc_tls_version::TLS1_2;
  grpc_tls_version max_tls_version_ = grpc_tls_version::TLS1_3;
  std::shared_ptr<grpc_tls_certificate_verifier> certificate_verifier_;
  std::shared_ptr<grpc_tls_certificate_provider> certificate_provider_;
  bool watch_root_cert_ = false;
  std::string root_cert_name_;
  bool watch_identity_pair_ = false;
  std::string identity_cert_name_;
  std::string tls_session_key_log_file_path_;
  std::shared_ptr<grpc_core::CrlProvider> crl_provider_;
};

// Server-specific options for configuring TLS.
class TlsServerCredentialsBuilder final : public TlsCredentialsBuilder {
 public:
  // A certificate provider that provides identity credentials is required,
  // because a server must always present identity credentials during any TLS
  // handshake. The certificate provider may optionally provide root
  // certificates, in case the server requests client certificates.
  explicit TlsServerCredentialsBuilder(
      std::shared_ptr<CertificateProviderInterface> certificate_provider) {
    set_certificate_provider(certificate_provider);
  }

  // Builds a grpc_server_credentials instance that establishes TLS connections
  // in the manner specified by options.
  std::shared_ptr<grpc_server_credentials> BuildTlsServerCredentials();

  // Sets requirements for if client certificates are requested, if they
  // are required, and if the client certificate must be trusted. The
  // default is GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, which represents
  // normal TLS.
  void set_cert_request_type(
      grpc_ssl_client_certificate_request_type cert_request_type);

 private:
  grpc_ssl_client_certificate_request_type cert_request_type_ =
      GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
};

// Client-specific options for configuring TLS.
//
// A client may optionally set a certificate provider.
// If there is no certificate provider, the system default root certificates
// will be used to verify server certificates. If a certificate provider is set
// and it provides root certifices that root will be used. If a certificate
// provider is set and it provides identity credentials those identity
// credentials will be used.
class TlsChannelCredentialsBuilder final
    : public grpc_core::TlsCredentialsBuilder {
 public:
  // Sets the decision of whether to do a crypto check on the server
  // certificates. The default is true.
  void set_verify_server_certificates(bool verify_server_certs);

  // Builds a grpc_channel_credentials instance that establishes TLS connections
  // in the manner specified by options.
  std::shared_ptr<grpc_channel_credentials> BuildTlsChannelCredentials();

 private:
  bool verify_server_cert_ = true;
};

}  // namespace grpc_core

#endif /* GRPC_GRPC_TLS_H */