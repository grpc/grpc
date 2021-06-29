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
#include "tls_credentials_options_util.h"

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

DataWatcherCertificateProvider::DataWatcherCertificateProvider(
    const std::string& root_certificate,
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  GPR_ASSERT(!root_certificate.empty() || !identity_key_cert_pairs.empty());
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
    grpc_tls_identity_pairs_add_pair(pairs_core, pair.private_key.c_str(),
                                     pair.certificate_chain.c_str());
  }
  c_provider_ = grpc_tls_certificate_provider_in_memory_create(
      root_certificate.c_str(), pairs_core);
  GPR_ASSERT(c_provider_ != nullptr);

  std::string test_root_certificate = "initial_root_certificate";
  IdentityKeyCertPair identity_pair = {
      "initial_private_key",        // private_key
      "initial_certificate_chain",  // certificate_chain
  };
  auto identity_pairs = std::vector<IdentityKeyCertPair>(1, identity_pair);
  auto certificate_provider = std::make_shared<DataWatcherCertificateProvider>(
      root_certificate, identity_pairs);
  test_root_certificate = "updated_root_certificate";
  grpc::Status reload_root_status =
      certificate_provider->ReloadRootCertificate(test_root_certificate);
  identity_pair = {
      "updated_private_key",        // private_key
      "updated_certificate_chain",  // certificate_chain
  };
  identity_pairs.assign(1, identity_pair);
  grpc::Status reload_key_cert_pair_status =
      certificate_provider->ReloadKeyCertificatePair(identity_pairs);
}

DataWatcherCertificateProvider::~DataWatcherCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
}

grpc::Status DataWatcherCertificateProvider::ReloadRootCertificate(
    const string& root_certificate) {
  grpc_core::DataWatcherCertificateProvider* in_memory_provider =
      dynamic_cast<grpc_core::DataWatcherCertificateProvider*>(c_provider_);
  // TODO: replace with status
  GPR_ASSERT(in_memory_provider != nullptr);
  absl::Status status =
      in_memory_provider->ReloadRootCertificate(root_certificate);
  return grpc::Status(static_cast<StatusCode>(status.code()),
                      status.message().data());
}

grpc::Status DataWatcherCertificateProvider::ReloadKeyCertificatePair(
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
    grpc_tls_identity_pairs_add_pair(pairs_core, pair.private_key.c_str(),
                                     pair.certificate_chain.c_str());
  }
  grpc_core::DataWatcherCertificateProvider* in_memory_provider =
      dynamic_cast<grpc_core::DataWatcherCertificateProvider*>(c_provider_);
  // TODO: replace with status
  GPR_ASSERT(in_memory_provider != nullptr);
  absl::Status status = in_memory_provider->ReloadKeyCertificatePair(
      pairs_core->pem_key_cert_pairs);
  return grpc::Status(static_cast<StatusCode>(status.code()),
                      status.message().data());
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

ExternalCertificateProvider::ExternalCertificateProvider(
    const string& root_certificate,
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  GPR_ASSERT(!root_certificate.empty() || !identity_key_cert_pairs.empty());
  grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
  for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
    grpc_tls_identity_pairs_add_pair(pairs_core, pair.private_key.c_str(),
                                     pair.certificate_chain.c_str());
  }
  c_provider_ = grpc_tls_certificate_provider_external_create(
      root_certificate.c_str(), pairs_core);
  GPR_ASSERT(c_provider_ != nullptr);
}

// c_provider()->distributor()->
ExternalCertificateProvider::~ExternalCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
}

void ExternalCertificateProvider::SetKeyMaterials(
    const string& cert_name, const string& root_certificate,
    const std::vector<IdentityKeyCertPair>& identity_key_cert_pairs) {
  if (!root_certificate.empty()) {
    c_provider()->distributor()->SetKeyMaterials(cert_name, root_certificate,
                                                 absl::nullopt);
  }
  if (!identity_key_cert_pairs.empty()) {
    grpc_tls_identity_pairs* pairs_core = grpc_tls_identity_pairs_create();
    for (const IdentityKeyCertPair& pair : identity_key_cert_pairs) {
      grpc_tls_identity_pairs_add_pair(pairs_core, pair.private_key.c_str(),
                                       pair.certificate_chain.c_str());
    }
    c_provider()->distributor()->SetKeyMaterials(
        cert_name, absl::nullopt, pairs_core->pem_key_cert_pairs);
  }
}

bool ExternalCertificateProvider::HasRootCerts(const std::string& cert_name) {
  return c_provider()->distributor()->HasRootCerts(cert_name);
}

bool ExternalCertificateProvider::HasKeyCertPairs(const string& cert_name) {
  return c_provider()->distributor()->HasKeyCertPairs(cert_name);
}

void ExternalCertificateProvider::SetErrorForCert(
    const string& cert_name, const string& root_cert_error,
    const string& identity_cert_error) {
  grpc_error_handle root_cert_error_handle =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(root_cert_error.c_str());
  grpc_error_handle identity_cert_error_handle =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(identity_cert_error.c_str());
  c_provider()->distributor()->SetErrorForCert(
      cert_name,
      root_cert_error.empty() ? GRPC_ERROR_NONE
                              : GRPC_ERROR_REF(root_cert_error_handle),
      identity_cert_error.empty() ? GRPC_ERROR_NONE
                                  : GRPC_ERROR_REF(identity_cert_error_handle));
  GRPC_ERROR_UNREF(root_cert_error_handle);
  GRPC_ERROR_UNREF(identity_cert_error_handle);
}

void ExternalCertificateProvider::SetError(const string& error) {
  if (!error.empty()) {
    grpc_error_handle error_handle =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(error.c_str());
    c_provider()->distributor()->SetError(GRPC_ERROR_REF(error_handle));
    GRPC_ERROR_UNREF(error_handle);
  }
}

void ExternalCertificateProvider::SetWatchStatusCallback(
    std::function<void(std::string, bool, bool)> callback) {
  c_provider()->distributor()->SetWatchStatusCallback(callback);
  std::unique_ptr<grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface> watcher;
}

//grpc::Status CustomReloadRootCertificate(const string& root_certificate) {
//
//}

}  // namespace experimental
}  // namespace grpc
