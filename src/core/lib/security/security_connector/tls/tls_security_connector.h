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

#include "absl/status/status.h"

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

using TlsSessionKeyLogger = tsi::TlsSessionKeyLoggerCache::TlsSessionKeyLogger;

namespace grpc_core {

// Channel security connector using TLS as transport security protocol.
class TlsChannelSecurityConnector final
    : public grpc_channel_security_connector {
 public:
  // static factory method to create a TLS channel security connector.
  static RefCountedPtr<grpc_channel_security_connector>
  CreateTlsChannelSecurityConnector(
      RefCountedPtr<grpc_channel_credentials> channel_creds,
      RefCountedPtr<grpc_tls_credentials_options> options,
      RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, const char* overridden_target_name,
      tsi_ssl_session_cache* ssl_session_cache);

  TlsChannelSecurityConnector(
      RefCountedPtr<grpc_channel_credentials> channel_creds,
      RefCountedPtr<grpc_tls_credentials_options> options,
      RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, const char* overridden_target_name,
      tsi_ssl_session_cache* ssl_session_cache);

  ~TlsChannelSecurityConnector() override;

  void add_handshakers(const grpc_channel_args* args,
                       grpc_pollset_set* interested_parties,
                       HandshakeManager* handshake_mgr) override;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  void cancel_check_peer(grpc_closure* on_peer_checked,
                         grpc_error_handle error) override;

  int cmp(const grpc_security_connector* other_sc) const override;

  ArenaPromise<absl::Status> CheckCallHost(
      absl::string_view host, grpc_auth_context* auth_context) override;

  tsi_ssl_client_handshaker_factory* ClientHandshakerFactoryForTesting() {
    MutexLock lock(&mu_);
    return client_handshaker_factory_;
  };

  absl::optional<absl::string_view> RootCertsForTesting() {
    MutexLock lock(&mu_);
    return pem_root_certs_;
  }

  absl::optional<PemKeyCertPairList> KeyCertPairListForTesting() {
    MutexLock lock(&mu_);
    return pem_key_cert_pair_list_;
  }

 private:
  // A watcher that watches certificate updates from
  // grpc_tls_certificate_distributor. It will never outlive
  // |security_connector_|.
  class TlsChannelCertificateWatcher : public grpc_tls_certificate_distributor::
                                           TlsCertificatesWatcherInterface {
   public:
    explicit TlsChannelCertificateWatcher(
        TlsChannelSecurityConnector* security_connector)
        : security_connector_(security_connector) {}
    void OnCertificatesChanged(
        absl::optional<absl::string_view> root_certs,
        absl::optional<PemKeyCertPairList> key_cert_pairs) override;
    void OnError(grpc_error_handle root_cert_error,
                 grpc_error_handle identity_cert_error) override;

   private:
    TlsChannelSecurityConnector* security_connector_ = nullptr;
  };

  // Use "new" to create a new instance, and no need to delete it later, since
  // it will be self-destroyed in |OnVerifyDone|.
  class ChannelPendingVerifierRequest {
   public:
    ChannelPendingVerifierRequest(
        RefCountedPtr<TlsChannelSecurityConnector> security_connector,
        grpc_closure* on_peer_checked, tsi_peer peer, const char* target_name);

    ~ChannelPendingVerifierRequest();

    void Start();

    grpc_tls_custom_verification_check_request* request() { return &request_; }

   private:
    void OnVerifyDone(bool run_callback_inline, absl::Status status);
    // The request will keep a reference of the security connector to make sure
    // it won't be destroyed while the request is still ongoing.
    RefCountedPtr<TlsChannelSecurityConnector> security_connector_;
    grpc_tls_custom_verification_check_request request_;
    grpc_closure* on_peer_checked_;
  };

  // Updates |client_handshaker_factory_| when the certificates that
  // |certificate_watcher_| is watching get updated.
  grpc_security_status UpdateHandshakerFactoryLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  Mutex mu_;
  // We need a separate mutex for |pending_verifier_requests_|, otherwise there
  // would be deadlock errors.
  Mutex verifier_request_map_mu_;
  RefCountedPtr<grpc_tls_credentials_options> options_;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      certificate_watcher_ = nullptr;
  std::string target_name_;
  std::string overridden_target_name_;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_
      ABSL_GUARDED_BY(mu_) = nullptr;
  tsi_ssl_session_cache* ssl_session_cache_ ABSL_GUARDED_BY(mu_) = nullptr;
  RefCountedPtr<TlsSessionKeyLogger> tls_session_key_logger_;
  absl::optional<absl::string_view> pem_root_certs_ ABSL_GUARDED_BY(mu_);
  absl::optional<PemKeyCertPairList> pem_key_cert_pair_list_
      ABSL_GUARDED_BY(mu_);
  std::map<grpc_closure* /*on_peer_checked*/, ChannelPendingVerifierRequest*>
      pending_verifier_requests_ ABSL_GUARDED_BY(verifier_request_map_mu_);
};

// Server security connector using TLS as transport security protocol.
class TlsServerSecurityConnector final : public grpc_server_security_connector {
 public:
  // static factory method to create a TLS server security connector.
  static RefCountedPtr<grpc_server_security_connector>
  CreateTlsServerSecurityConnector(
      RefCountedPtr<grpc_server_credentials> server_creds,
      RefCountedPtr<grpc_tls_credentials_options> options);

  TlsServerSecurityConnector(
      RefCountedPtr<grpc_server_credentials> server_creds,
      RefCountedPtr<grpc_tls_credentials_options> options);
  ~TlsServerSecurityConnector() override;

  void add_handshakers(const grpc_channel_args* args,
                       grpc_pollset_set* interested_parties,
                       HandshakeManager* handshake_mgr) override;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  void cancel_check_peer(grpc_closure* /*on_peer_checked*/,
                         grpc_error_handle error) override;

  int cmp(const grpc_security_connector* other) const override;

  tsi_ssl_server_handshaker_factory* ServerHandshakerFactoryForTesting() {
    MutexLock lock(&mu_);
    return server_handshaker_factory_;
  };

  const absl::optional<absl::string_view>& RootCertsForTesting() {
    MutexLock lock(&mu_);
    return pem_root_certs_;
  }

  const absl::optional<PemKeyCertPairList>& KeyCertPairListForTesting() {
    MutexLock lock(&mu_);
    return pem_key_cert_pair_list_;
  }

 private:
  // A watcher that watches certificate updates from
  // grpc_tls_certificate_distributor. It will never outlive
  // |security_connector_|.
  class TlsServerCertificateWatcher : public grpc_tls_certificate_distributor::
                                          TlsCertificatesWatcherInterface {
   public:
    explicit TlsServerCertificateWatcher(
        TlsServerSecurityConnector* security_connector)
        : security_connector_(security_connector) {}
    void OnCertificatesChanged(
        absl::optional<absl::string_view> root_certs,
        absl::optional<PemKeyCertPairList> key_cert_pairs) override;

    void OnError(grpc_error_handle root_cert_error,
                 grpc_error_handle identity_cert_error) override;

   private:
    TlsServerSecurityConnector* security_connector_ = nullptr;
  };

  // Use "new" to create a new instance, and no need to delete it later, since
  // it will be self-destroyed in |OnVerifyDone|.
  class ServerPendingVerifierRequest {
   public:
    ServerPendingVerifierRequest(
        RefCountedPtr<TlsServerSecurityConnector> security_connector,
        grpc_closure* on_peer_checked, tsi_peer peer);

    ~ServerPendingVerifierRequest();

    void Start();

    grpc_tls_custom_verification_check_request* request() { return &request_; }

   private:
    void OnVerifyDone(bool run_callback_inline, absl::Status status);
    // The request will keep a reference of the security connector to make sure
    // it won't be destroyed while the request is still ongoing.
    RefCountedPtr<TlsServerSecurityConnector> security_connector_;
    grpc_tls_custom_verification_check_request request_;
    grpc_closure* on_peer_checked_;
  };

  // Updates |server_handshaker_factory_| when the certificates that
  // |certificate_watcher_| is watching get updated.
  grpc_security_status UpdateHandshakerFactoryLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  Mutex mu_;
  // We need a separate mutex for |pending_verifier_requests_|, otherwise there
  // would be deadlock errors.
  Mutex verifier_request_map_mu_;
  RefCountedPtr<grpc_tls_credentials_options> options_;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      certificate_watcher_ = nullptr;
  tsi_ssl_server_handshaker_factory* server_handshaker_factory_
      ABSL_GUARDED_BY(mu_) = nullptr;
  absl::optional<absl::string_view> pem_root_certs_ ABSL_GUARDED_BY(mu_);
  absl::optional<PemKeyCertPairList> pem_key_cert_pair_list_
      ABSL_GUARDED_BY(mu_);
  RefCountedPtr<TlsSessionKeyLogger> tls_session_key_logger_;
  std::map<grpc_closure* /*on_peer_checked*/, ServerPendingVerifierRequest*>
      pending_verifier_requests_ ABSL_GUARDED_BY(verifier_request_map_mu_);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_TLS_SECURITY_CONNECTOR_H
