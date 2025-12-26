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

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_private_key_signer.h>

#include <string>
#include <vector>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/match.h"

namespace grpc {
namespace experimental {

StaticDataCertificateProvider::StaticDataCertificateProvider(
    const std::string& root_certificate,
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  GRPC_CHECK(!root_certificate.empty() || !identity_key_cert_pairs.empty());
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
    grpc_core::Match(
        pair.private_key,
        [&](const std::string& pem_root_certs) {
          grpc_tls_identity_pairs_add_pair(pairs_core, pem_root_certs.c_str(),
                                           pair.certificate_chain.c_str());
        },
        [&](std::shared_ptr<grpc::experimental::PrivateKeySigner> key_sign) {
          grpc_tls_identity_pairs_add_pair_with_signer(
              pairs_core, key_sign, pair.certificate_chain.c_str());
        });
  }
  c_provider_ = grpc_tls_certificate_provider_in_memory_create();
  GRPC_CHECK_NE(c_provider_, nullptr);
  grpc_tls_certificate_provider_in_memory_set_root_certificate(
      c_provider_, root_certificate.c_str());
  grpc_tls_certificate_provider_in_memory_set_identity_certificate(c_provider_,
                                                                   pairs_core);
};

StaticDataCertificateProvider::~StaticDataCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
};

absl::Status StaticDataCertificateProvider::ValidateCredentials() const {
  auto* provider =
      grpc_core::DownCast<grpc_core::InMemoryCertificateProvider*>(c_provider_);
  return provider->ValidateCredentials();
}

FileWatcherCertificateProvider::FileWatcherCertificateProvider(
    const std::string& private_key_path,
    const std::string& identity_certificate_path,
    const std::string& root_cert_path,
    const std::string& spiffe_bundle_map_path,
    unsigned int refresh_interval_sec) {
  c_provider_ = grpc_tls_certificate_provider_file_watcher_create(
      private_key_path.c_str(), identity_certificate_path.c_str(),
      root_cert_path.c_str(), spiffe_bundle_map_path.c_str(),
      refresh_interval_sec);
  GRPC_CHECK_NE(c_provider_, nullptr);
};

FileWatcherCertificateProvider::~FileWatcherCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
};

absl::Status FileWatcherCertificateProvider::ValidateCredentials() const {
  auto* provider =
      grpc_core::DownCast<grpc_core::FileWatcherCertificateProvider*>(
          c_provider_);
  return provider->ValidateCredentials();
}

InMemoryCertificateProvider::InMemoryCertificateProvider() {
  c_provider_ = grpc_tls_certificate_provider_in_memory_create();
  GRPC_CHECK_NE(c_provider_, nullptr);
};

InMemoryCertificateProvider::~InMemoryCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
};

void InMemoryCertificateProvider::UpdateRoot(
    const std::string& root_certificate) {
  GRPC_CHECK(!root_certificate.empty());
  grpc_tls_certificate_provider_in_memory_set_root_certificate(
      c_provider_, root_certificate.c_str());
}

void InMemoryCertificateProvider::UpdateIdentity(
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  GRPC_CHECK(!identity_key_cert_pairs.empty());
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
    grpc_core::Match(
        pair.private_key,
        [&](const std::string& pem_root_certs) {
          grpc_tls_identity_pairs_add_pair(pairs_core, pem_root_certs.c_str(),
                                           pair.certificate_chain.c_str());
        },
        [&](std::shared_ptr<grpc::experimental::PrivateKeySigner> key_sign) {
          grpc_tls_identity_pairs_add_pair_with_signer(
              pairs_core, key_sign, pair.certificate_chain.c_str());
        });
  }
  grpc_tls_certificate_provider_in_memory_set_identity_certificate(c_provider_,
                                                                   pairs_core);
}

absl::Status InMemoryCertificateProvider::ValidateCredentials() const {
  auto* provider =
      grpc_core::DownCast<grpc_core::InMemoryCertificateProvider*>(c_provider_);
  return provider->ValidateCredentials();
}

}  // namespace experimental
}  // namespace grpc
