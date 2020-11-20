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

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpcpp/security/tls_certificate_provider.h>

#include "absl/container/inlined_vector.h"

namespace grpc {
namespace experimental {

StaticDataCertificateProvider::StaticDataCertificateProvider(
    const std::string& root_certificate,
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  GPR_ASSERT(!root_certificate.empty() || !identity_key_cert_pairs.empty());
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
    grpc_tls_identity_pairs_add_pair(pairs_core, pair.private_key.c_str(),
                                     pair.certificate_chain.c_str());
  }
  c_provider_ = grpc_tls_certificate_provider_static_data_create(
      root_certificate.c_str(), pairs_core);
  GPR_ASSERT(c_provider_ != nullptr);
};

StaticDataCertificateProvider::~StaticDataCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
};

FileWatcherCertificateProvider::FileWatcherCertificateProvider(
    const std::string& private_key_path,
    const std::string& identity_certificate_path,
    const std::string& root_cert_path, unsigned int refresh_interval_sec) {
  c_provider_ = grpc_tls_certificate_provider_file_watcher_create(
      private_key_path.c_str(), identity_certificate_path.c_str(),
      root_cert_path.c_str(), refresh_interval_sec);
  GPR_ASSERT(c_provider_ != nullptr);
};

FileWatcherCertificateProvider::~FileWatcherCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
};

}  // namespace experimental
}  // namespace grpc
