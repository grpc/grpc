//
//
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_CERTIFICATE_PROVIDER_H
#define GRPC_CORE_EXT_XDS_XDS_CERTIFICATE_PROVIDER_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_api.h"
#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#define GRPC_ARG_XDS_CERTIFICATE_PROVIDER \
  "grpc.internal.xds_certificate_provider"

namespace grpc_core {

class XdsCertificateProvider : public grpc_tls_certificate_provider {
 public:
  XdsCertificateProvider();
  ~XdsCertificateProvider() override;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

  bool ProvidesRootCerts(const std::string& cert_name);
  void UpdateRootCertNameAndDistributor(
      const std::string& cert_name, absl::string_view root_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor);

  bool ProvidesIdentityCerts(const std::string& cert_name);
  void UpdateIdentityCertNameAndDistributor(
      const std::string& cert_name, absl::string_view identity_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor>
          identity_cert_distributor);

  bool GetRequireClientCertificate(const std::string& cert_name);
  // Updating \a require_client_certificate for a non-existing \a cert_name has
  // no effect.
  void UpdateRequireClientCertificate(const std::string& cert_name,
                                      bool require_client_certificate);

  std::vector<StringMatcher> GetSanMatchers(const std::string& cluster);
  void UpdateSubjectAlternativeNameMatchers(
      const std::string& cluster, std::vector<StringMatcher> matchers);

  grpc_arg MakeChannelArg() const;

  static RefCountedPtr<XdsCertificateProvider> GetFromChannelArgs(
      const grpc_channel_args* args);

 private:
  class ClusterCertificateState {
   public:
    explicit ClusterCertificateState(
        XdsCertificateProvider* xds_certificate_provider)
        : xds_certificate_provider_(xds_certificate_provider) {}

    ~ClusterCertificateState();

    // Returns true if the certs aren't being watched and there are no
    // distributors configured.
    bool IsSafeToRemove() const;

    bool ProvidesRootCerts() const { return root_cert_distributor_ != nullptr; }
    bool ProvidesIdentityCerts() const {
      return identity_cert_distributor_ != nullptr;
    }

    void UpdateRootCertNameAndDistributor(
        const std::string& cert_name, absl::string_view root_cert_name,
        RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor);
    void UpdateIdentityCertNameAndDistributor(
        const std::string& cert_name, absl::string_view identity_cert_name,
        RefCountedPtr<grpc_tls_certificate_distributor>
            identity_cert_distributor);

    void UpdateRootCertWatcher(
        const std::string& cert_name,
        grpc_tls_certificate_distributor* root_cert_distributor);
    void UpdateIdentityCertWatcher(
        const std::string& cert_name,
        grpc_tls_certificate_distributor* identity_cert_distributor);

    bool require_client_certificate() const {
      return require_client_certificate_;
    }
    void set_require_client_certificate(bool require_client_certificate) {
      require_client_certificate_ = require_client_certificate;
    }

    void WatchStatusCallback(const std::string& cert_name,
                             bool root_being_watched,
                             bool identity_being_watched);

   private:
    XdsCertificateProvider* xds_certificate_provider_;
    bool watching_root_certs_ = false;
    bool watching_identity_certs_ = false;
    std::string root_cert_name_;
    std::string identity_cert_name_;
    RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor_;
    RefCountedPtr<grpc_tls_certificate_distributor> identity_cert_distributor_;
    grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
        root_cert_watcher_ = nullptr;
    grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
        identity_cert_watcher_ = nullptr;
    bool require_client_certificate_ = false;
  };

  void WatchStatusCallback(std::string cert_name, bool root_being_watched,
                           bool identity_being_watched);

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;

  Mutex mu_;
  std::map<std::string /*cert_name*/, std::unique_ptr<ClusterCertificateState>>
      certificate_state_map_ ABSL_GUARDED_BY(mu_);

  // Use a separate mutex for san_matchers_ to avoid deadlocks since
  // san_matchers_ needs to be accessed when a handshake is being done and we
  // run into a possible deadlock scenario if using the same mutex. The mutex
  // deadlock cycle is formed as -
  // WatchStatusCallback() -> SetKeyMaterials() ->
  // TlsChannelSecurityConnector::TlsChannelCertificateWatcher::OnCertificatesChanged()
  // -> HandshakeManager::Add() -> SecurityHandshaker::DoHandshake() ->
  // subject_alternative_names_matchers()
  Mutex san_matchers_mu_;
  std::map<std::string /*cluster_name*/, std::vector<StringMatcher>>
      san_matcher_map_ ABSL_GUARDED_BY(san_matchers_mu_);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CERTIFICATE_PROVIDER_H
