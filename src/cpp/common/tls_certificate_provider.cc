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

#include <string>
#include <vector>

#include <grpc/grpc_security.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/support/config.h>
#include <grpcpp/support/status.h>

namespace grpc {
namespace experimental {

InMemoryCertificateProvider::InMemoryCertificateProvider() {
  c_provider_ = grpc_tls_certificate_provider_in_memory_create();
  GPR_ASSERT(c_provider_ != nullptr);
};

InMemoryCertificateProvider::~InMemoryCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
};

grpc::Status InMemoryCertificateProvider::SetRootCertificate(
    std::string root_certificate) {
  GPR_ASSERT(!root_certificate.empty());
  const char* error_details;
  grpc::Status status;
  grpc_status_code code = grpc_tls_certificate_provider_in_memory_set_root_cert(
      c_provider_, root_certificate.c_str(), &error_details);
  if (code != GRPC_STATUS_OK) {
    status = grpc::Status(static_cast<grpc::StatusCode>(code), error_details);
    gpr_free(const_cast<char*>(error_details));
  }
  return status;
}

grpc::Status InMemoryCertificateProvider::SetKeyCertificatePairs(
    std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  GPR_ASSERT(!identity_key_cert_pairs.empty());
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
    grpc_tls_identity_pairs_add_pair(pairs_core, pair.private_key.c_str(),
                                     pair.certificate_chain.c_str());
  }
  const char* error_details;
  grpc::Status status;
  grpc_status_code code =
      grpc_tls_certificate_provider_in_memory_set_key_cert_pairs(
          c_provider_, pairs_core, &error_details);
  if (code != GRPC_STATUS_OK) {
    status = grpc::Status(static_cast<grpc::StatusCode>(code), error_details);
    gpr_free(const_cast<char*>(error_details));
  }
  return status;
}

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
