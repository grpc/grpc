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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

namespace grpc_core {

class XdsCertificateProvider : public grpc_tls_certificate_provider {
 public:
  XdsCertificateProvider(
      absl::string_view root_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor,
      absl::string_view identity_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor>
          identity_cert_distributor);

  void UpdateRootCertNameAndDistributor(
      absl::string_view root_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor);
  void UpdateIdentityCertNameAndDistributor(
      absl::string_view identity_cert_name,
      RefCountedPtr<grpc_tls_certificate_distributor>
          identity_cert_distributor);

  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor()
      const override {
    return distributor_;
  }

 private:
  void WatchStatusCallback(std::string cert_name, bool root_being_watched,
                           bool identity_being_watched);
  void UpdateRootCertWatcher(
      grpc_tls_certificate_distributor* root_cert_distributor);
  void UpdateIdentityCertWatcher(
      grpc_tls_certificate_distributor* identity_cert_distributor);

  Mutex mu_;
  bool watching_root_certs_ = false;
  bool watching_identity_certs_ = false;
  std::string root_cert_name_;
  std::string identity_cert_name_;
  RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor_;
  RefCountedPtr<grpc_tls_certificate_distributor> identity_cert_distributor_;
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      root_cert_watcher_ = nullptr;
  grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface*
      identity_cert_watcher_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CERTIFICATE_PROVIDER_H
