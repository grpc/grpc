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
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#define GRPC_ARG_XDS_CERTIFICATE_PROVIDER \
  "grpc.internal.xds_certificate_provider"

namespace grpc_core {

class XdsCertificateProvider : public grpc_tls_certificate_provider {
 public:
  XdsCertificateProvider(
      absl::string_view root_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor,
      absl::string_view identity_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor> identity_cert_distributor,
      std::vector<XdsApi::StringMatcher> san_matchers);

  ~XdsCertificateProvider() override;

  void UpdateRootCertNameAndDistributor(
      absl::string_view root_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor);
  void UpdateIdentityCertNameAndDistributor(
      absl::string_view identity_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor>
          identity_cert_distributor);
  void UpdateSubjectAlternativeNameMatchers(
      std::vector<XdsApi::StringMatcher> matchers);

  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor()
      const override {
    return distributor_;
  }

  bool ProvidesRootCerts() {
    MutexLock lock(&mu_);
    return root_cert_distributor_ != nullptr;
  }

  bool ProvidesIdentityCerts() {
    MutexLock lock(&mu_);
    return identity_cert_distributor_ != nullptr;
  }

  std::vector<XdsApi::StringMatcher> subject_alternative_name_matchers() {
    MutexLock lock(&san_matchers_mu_);
    return san_matchers_;
  }

  grpc_arg MakeChannelArg() const;

  static RefCountedPtr<XdsCertificateProvider> GetFromChannelArgs(
      const grpc_channel_args* args);

 private:
  void WatchStatusCallback(std::string cert_name, bool root_being_watched,
                           bool identity_being_watched);
  void UpdateRootCertWatcher(
      grpc_tls_certificate_distributor* root_cert_distributor);
  void UpdateIdentityCertWatcher(
      grpc_tls_certificate_distributor* identity_cert_distributor);

  Mutex mu_;
  // Use a separate mutex for san_matchers_ to avoid deadlocks since
  // san_matchers_ needs to be accessed when a handshake is being done and we
  // run into a possible deadlock scenario if using the same mutex. The mutex
  // deadlock cycle is formed as -
  // WatchStatusCallback() -> SetKeyMaterials() ->
  // TlsChannelSecurityConnector::TlsChannelCertificateWatcher::OnCertificatesChanged()
  // -> HandshakeManager::Add() -> SecurityHandshaker::DoHandshake() ->
  // subject_alternative_names_matchers()
  Mutex san_matchers_mu_;
  bool watching_root_certs_ = false;
  bool watching_identity_certs_ = false;
  std::string root_cert_name_;
  std::string identity_cert_name_;
  RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor_;
  RefCountedPtr<grpc_tls_certificate_distributor> identity_cert_distributor_;
  std::vector<XdsApi::StringMatcher> san_matchers_;
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      root_cert_watcher_ = nullptr;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      identity_cert_watcher_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CERTIFICATE_PROVIDER_H
