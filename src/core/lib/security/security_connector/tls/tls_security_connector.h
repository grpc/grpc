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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_TLS_SECURITY_CONNECTOR_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_TLS_SECURITY_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#define GRPC_TLS_TRANSPORT_SECURITY_TYPE "tls"

namespace grpc_core {

// Forward declaration.
class TlsChannelSecurityConnector;

class TlsChannelCertificateWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  TlsChannelCertificateWatcher(TlsChannelSecurityConnector* security_connector)
      : security_connector_(security_connector) {}
  void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) override;
  void OnError(grpc_error* root_cert_error,
               grpc_error* identity_cert_error) override;

 private:
  TlsChannelSecurityConnector* security_connector_ = nullptr;
};

// TLS channel security connector.
class TlsChannelSecurityConnector final
    : public grpc_channel_security_connector {
 public:
  // static factory method to create a TLS channel security connector.
  static grpc_core::RefCountedPtr<grpc_channel_security_connector>
  CreateTlsChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, const char* overridden_target_name,
      tsi_ssl_session_cache* ssl_session_cache);

  TlsChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, const char* overridden_target_name,
      tsi_ssl_session_cache* ssl_session_cache);
  ~TlsChannelSecurityConnector() override;

  void add_handshakers(const grpc_channel_args* args,
                       grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  int cmp(const grpc_security_connector* other_sc) const override;

  bool check_call_host(absl::string_view host, grpc_auth_context* auth_context,
                       grpc_closure* on_call_host_checked,
                       grpc_error** error) override;

  void cancel_check_call_host(grpc_closure* on_call_host_checked,
                              grpc_error* error) override;

  // Updates SSL TSI client handshaker factory.
  grpc_security_status UpdateHandshakerFactoryLocked(
      tsi_ssl_session_cache* ssl_session_cache);

  tsi_ssl_client_handshaker_factory* ClientHandshakerFactoryForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return client_handshaker_factory_;
  };

  const absl::optional<absl::string_view>& RootCertsForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return pem_root_certs_;
  }

  const absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>&
  KeyCertPairListForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return pem_key_cert_pair_list_;
  }

 private:
  // gRPC-provided callback executed by application, which servers to bring the
  // control back to gRPC core.
  static void ServerAuthorizationCheckDone(
      grpc_tls_server_authorization_check_arg* arg);

  // A util function to process server authorization check result.
  static grpc_error* ProcessServerAuthorizationCheckResult(
      grpc_tls_server_authorization_check_arg* arg);

  // A util function to create a server authorization check arg instance.
  static grpc_tls_server_authorization_check_arg*
  ServerAuthorizationCheckArgCreate(void* user_data);

  // A util function to destroy a server authorization check arg instance.
  static void ServerAuthorizationCheckArgDestroy(
      grpc_tls_server_authorization_check_arg* arg);

  grpc_core::Mutex mu_;
  std::set<grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*>
      certificate_watcher_set_;
  grpc_closure* on_peer_checked_ = nullptr;
  std::string target_name_;
  std::string overridden_target_name_;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_ = nullptr;
  grpc_tls_server_authorization_check_arg* check_arg_ = nullptr;
  // Contains resumption information only useful when the credential is avaible
  // at the time security connector is created. It will become invalid after
  // reloading.
  tsi_ssl_session_cache* ssl_session_cache_ = nullptr;
  absl::optional<absl::string_view> pem_root_certs_;
  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
      pem_key_cert_pair_list_;

  friend void TlsChannelCertificateWatcher::OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs);
};

// Forward declaration.
class TlsServerSecurityConnector;

class TlsServerCertificateWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  TlsServerCertificateWatcher(TlsServerSecurityConnector* security_connector)
      : security_connector_(security_connector) {}
  void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) override;
  void OnError(grpc_error* root_cert_error,
               grpc_error* identity_cert_error) override;

 private:
  TlsServerSecurityConnector* security_connector_ = nullptr;
};

// TLS server security connector.
class TlsServerSecurityConnector final : public grpc_server_security_connector {
 public:
  // static factory method to create a TLS server security connector.
  static grpc_core::RefCountedPtr<grpc_server_security_connector>
  CreateTlsServerSecurityConnector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds);

  explicit TlsServerSecurityConnector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds);
  ~TlsServerSecurityConnector() override;

  void add_handshakers(const grpc_channel_args* args,
                       grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  int cmp(const grpc_security_connector* other) const override;

  // Updates SSL TSI client handshaker factory.
  grpc_security_status UpdateHandshakerFactoryLocked();

  tsi_ssl_server_handshaker_factory* ServerHandshakerFactoryForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return server_handshaker_factory_;
  };

  const absl::optional<absl::string_view>& RootCertsForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return pem_root_certs_;
  }

  const absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>&
  KeyCertPairListForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return pem_key_cert_pair_list_;
  }

 private:
  grpc_core::Mutex mu_;
  std::set<grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*>
      certificate_watcher_set_;

  tsi_ssl_server_handshaker_factory* server_handshaker_factory_ = nullptr;
  absl::optional<absl::string_view> pem_root_certs_;
  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
      pem_key_cert_pair_list_;

  friend void TlsServerCertificateWatcher::OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs);
};

// ---- Functions below are exposed for testing only -----------------------

// TlsCheckHostName checks if |peer_name| matches the identity information
// contained in |peer|. This is AKA hostname check.
grpc_error* TlsCheckHostName(const char* peer_name, const tsi_peer* peer);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_TLS_SECURITY_CONNECTOR_H \
        */