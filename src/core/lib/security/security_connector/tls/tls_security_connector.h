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
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#define GRPC_TLS_TRANSPORT_SECURITY_TYPE "tls"

namespace grpc_core {

// Channel security connector using TLS as transport security protocol.
class TlsChannelSecurityConnector final
    : public grpc_channel_security_connector {
 public:
  // static factory method to create a TLS channel security connector.
  static grpc_core::RefCountedPtr<grpc_channel_security_connector>
  CreateTlsChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_tls_credentials_options> options,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, const char* overridden_target_name,
      tsi_ssl_session_cache* ssl_session_cache);

  TlsChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_tls_credentials_options> options,
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

  tsi_ssl_client_handshaker_factory* ClientHandshakerFactoryForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return client_handshaker_factory_;
  };

  absl::optional<absl::string_view> RootCertsForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return pem_root_certs_;
  }

  absl::optional<grpc_core::PemKeyCertPairList> KeyCertPairListForTesting() {
    grpc_core::MutexLock lock(&mu_);
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
        absl::optional<grpc_core::PemKeyCertPairList> key_cert_pairs) override;
    void OnError(grpc_error* root_cert_error,
                 grpc_error* identity_cert_error) override;

   private:
    TlsChannelSecurityConnector* security_connector_ = nullptr;
  };

  // Use "new" to create a new instance, and no need to delete it later, since
  // it will be self-destroyed in |OnVerifyDone|.
  class ChannelPendingVerifierRequest final : public PendingVerifierRequest {
   public:
    ChannelPendingVerifierRequest(
        RefCountedPtr<TlsChannelSecurityConnector> security_connector,
        grpc_closure* on_peer_checked, tsi_peer peer, const char* target_name)
        : PendingVerifierRequest(on_peer_checked, std::move(peer)),
          security_connector_(security_connector) {
      // We can pass the existing string cached in security connector because
      // the verifier holds a ref to the security connector until this
      // verification request is completed.
      this->request_.target_name = target_name;
    }

    ~ChannelPendingVerifierRequest() {}

    void Start() {
      grpc_tls_certificate_verifier* verifier =
          security_connector_->options_->certificate_verifier();
      bool is_done =
          verifier->Verify(&request_, [this]() { OnVerifyDone(true); });
      if (is_done) OnVerifyDone(false);
    }

   private:
    void OnVerifyDone(bool run_callback_inline);
    // The request will keep a reference of the security connector to make sure
    // it won't be destroyed while the request is still ongoing.
    RefCountedPtr<TlsChannelSecurityConnector> security_connector_;
  };

  // Updates |client_handshaker_factory_| when the certificates that
  // |certificate_watcher_| is watching get updated.
  grpc_security_status UpdateHandshakerFactoryLocked();

  grpc_core::Mutex mu_;
  // We need a separate mutex for |pending_verifier_requests_|, otherwise there
  // would be deadlock errors.
  grpc_core::Mutex verifier_request_map_mu_;
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options_;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      certificate_watcher_ = nullptr;
  std::string target_name_;
  std::string overridden_target_name_;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_ = nullptr;
  tsi_ssl_session_cache* ssl_session_cache_ = nullptr;
  absl::optional<absl::string_view> pem_root_certs_;
  absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pair_list_;
  std::map<grpc_closure* /*on_peer_checked*/, PendingVerifierRequest*>
      pending_verifier_requests_;
};

// Server security connector using TLS as transport security protocol.
class TlsServerSecurityConnector final : public grpc_server_security_connector {
 public:
  // static factory method to create a TLS server security connector.
  static grpc_core::RefCountedPtr<grpc_server_security_connector>
  CreateTlsServerSecurityConnector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds,
      grpc_core::RefCountedPtr<grpc_tls_credentials_options> options);

  TlsServerSecurityConnector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds,
      grpc_core::RefCountedPtr<grpc_tls_credentials_options> options);
  ~TlsServerSecurityConnector() override;

  void add_handshakers(const grpc_channel_args* args,
                       grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  int cmp(const grpc_security_connector* other) const override;

  tsi_ssl_server_handshaker_factory* ServerHandshakerFactoryForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return server_handshaker_factory_;
  };

  const absl::optional<absl::string_view>& RootCertsForTesting() {
    grpc_core::MutexLock lock(&mu_);
    return pem_root_certs_;
  }

  const absl::optional<grpc_core::PemKeyCertPairList>&
  KeyCertPairListForTesting() {
    grpc_core::MutexLock lock(&mu_);
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
        absl::optional<grpc_core::PemKeyCertPairList> key_cert_pairs) override;
    void OnError(grpc_error* root_cert_error,
                 grpc_error* identity_cert_error) override;

   private:
    TlsServerSecurityConnector* security_connector_ = nullptr;
  };

  // Use "new" to create a new instance, and no need to delete it later, since
  // it will be self-destroyed in |OnVerifyDone|.
  class ServerPendingVerifierRequest final : public PendingVerifierRequest {
   public:
    ServerPendingVerifierRequest(
        RefCountedPtr<TlsServerSecurityConnector> security_connector,
        grpc_closure* on_peer_checked, tsi_peer peer)
        : PendingVerifierRequest(on_peer_checked, std::move(peer)),
          security_connector_(security_connector) {}

    ~ServerPendingVerifierRequest() {}

    void Start() {
      grpc_tls_certificate_verifier* verifier =
          security_connector_->options_->certificate_verifier();
      bool is_done =
          verifier->Verify(&request_, [this]() { OnVerifyDone(true); });
      if (is_done) OnVerifyDone(false);
    }

   private:
    void OnVerifyDone(bool run_callback_inline);
    // The request will keep a reference of the security connector to make sure
    // it won't be destroyed while the request is still ongoing.
    RefCountedPtr<TlsServerSecurityConnector> security_connector_;
  };

  // Updates |server_handshaker_factory_| when the certificates that
  // |certificate_watcher_| is watching get updated.
  grpc_security_status UpdateHandshakerFactoryLocked();

  grpc_core::Mutex mu_;
  // We need a separate mutex for |pending_verifier_requests_|, otherwise there
  // would be deadlock errors.
  grpc_core::Mutex verifier_request_map_mu_;
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options_;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      certificate_watcher_ = nullptr;

  tsi_ssl_server_handshaker_factory* server_handshaker_factory_ = nullptr;
  absl::optional<absl::string_view> pem_root_certs_;
  absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pair_list_;
  std::map<grpc_closure* /*on_peer_checked*/, PendingVerifierRequest*>
      pending_verifier_requests_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_TLS_SECURITY_CONNECTOR_H
