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
#include "absl/status/statusor.h"

namespace grpc {
namespace experimental {

namespace {

grpc_tls_identity_pairs* CreatePairsCore(
    std::vector<IdentityKeyCertPair> identity_key_cert_pairs) {
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const auto& pair : identity_key_cert_pairs) {
    grpc_tls_identity_pairs_add_pair(pairs_core, pair.private_key.c_str(),
                                     pair.certificate_chain.c_str());
  }
  return pairs_core;
}

absl::StatusOr<grpc_tls_identity_pairs*> CreatePairsCore(
    std::vector<IdentityKeyOrSignerCertPair>
        identity_key_or_signer_cert_pairs) {
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (auto& pair : identity_key_or_signer_cert_pairs) {
    absl::Status status = grpc_core::MatchMutable(
        &pair.private_key,
        [&](std::string* pem_root_certs) {
          grpc_tls_identity_pairs_add_pair(pairs_core, pem_root_certs->c_str(),
                                           pair.certificate_chain.c_str());
          return absl::OkStatus();
        },
        [&](std::shared_ptr<grpc::experimental::PrivateKeySigner>* key_signer) {
          return grpc_tls_identity_pairs_add_pair_with_signer(
              pairs_core, std::move(*key_signer),
              pair.certificate_chain.c_str());
        });
    if (!status.ok()) {
      grpc_tls_identity_pairs_destroy(pairs_core);
      return status;
    }
  }
  return pairs_core;
}

}  // namespace

StaticDataCertificateProvider::StaticDataCertificateProvider(
    const std::string& root_certificate,
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  GRPC_CHECK(!root_certificate.empty() || !identity_key_cert_pairs.empty());
  auto* pairs_core = CreatePairsCore(identity_key_cert_pairs);
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

absl::Status InMemoryCertificateProvider::UpdateRoot(
    const std::string& root_certificate) {
  GRPC_CHECK(!root_certificate.empty());
  return grpc_tls_certificate_provider_in_memory_set_root_certificate(
             c_provider_, root_certificate.c_str())
             ? absl::OkStatus()
             : absl::InternalError("Unable to update root certificate");
}

absl::Status InMemoryCertificateProvider::UpdateIdentityKeyCertPair(
    std::vector<IdentityKeyCertPair> identity_key_cert_pairs) {
  GRPC_CHECK(!identity_key_cert_pairs.empty());
  auto* pairs_core = CreatePairsCore(std::move(identity_key_cert_pairs));
  return grpc_tls_certificate_provider_in_memory_set_identity_certificate(
             c_provider_, pairs_core)
             ? absl::OkStatus()
             : absl::InternalError("Unable to update identity certificate");
}

absl::Status InMemoryCertificateProvider::UpdateIdentityKeyCertPair(
    std::vector<IdentityKeyOrSignerCertPair>
        identity_key_or_signer_cert_pairs) {
  GRPC_CHECK(!identity_key_or_signer_cert_pairs.empty());
  auto pairs_core_or =
      CreatePairsCore(std::move(identity_key_or_signer_cert_pairs));
  if (!pairs_core_or.ok()) return pairs_core_or.status();
  return grpc_tls_certificate_provider_in_memory_set_identity_certificate(
             c_provider_, *pairs_core_or)
             ? absl::OkStatus()
             : absl::InternalError("Unable to update identity certificate");
}

absl::Status InMemoryCertificateProvider::ValidateCredentials() const {
  auto* provider =
      grpc_core::DownCast<grpc_core::InMemoryCertificateProvider*>(c_provider_);
  return provider->ValidateCredentials();
}

}  // namespace experimental
}  // namespace grpc
