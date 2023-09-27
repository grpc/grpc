//
//
// Copyright 2023 gRPC authors.
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

#include "src/core/lib/security/credentials/tls/grpc_tls_crl_provider.h"

#include <memory>
#include <vector>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/support/log.h>

namespace grpc_core {
namespace experimental {

CertificateInfoImpl::CertificateInfoImpl(absl::string_view issuer)
    : issuer_(issuer) {}

absl::string_view CertificateInfoImpl::GetIssuer() const { return issuer_; }

std::string IssuerFromCrl(X509_CRL* crl) {
  char* buf = X509_NAME_oneline(X509_CRL_get_issuer(crl), nullptr, 0);
  std::string ret = buf;
  OPENSSL_free(buf);
  return ret;
}

CrlImpl::CrlImpl(X509_CRL* crl) : crl_(crl), issuer_(IssuerFromCrl(crl)) {}

CrlImpl::~CrlImpl() { X509_CRL_free(crl_); }

X509_CRL* CrlImpl::crl() const { return crl_; }

std::string CrlImpl::Issuer() { return issuer_; }

absl::StatusOr<std::unique_ptr<Crl>> Crl::Parse(absl::string_view crl_string) {
  if (crl_string.size() >= INT_MAX) {
    return absl::InvalidArgumentError("crl_string cannot be of size INT_MAX");
  }
  BIO* crl_bio =
      BIO_new_mem_buf(crl_string.data(), static_cast<int>(crl_string.size()));
  // Errors on BIO
  if (crl_bio == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from crl string to BIO failed.");
  }
  X509_CRL* crl = PEM_read_bio_X509_CRL(crl_bio, nullptr, nullptr, nullptr);
  BIO_free(crl_bio);
  if (crl == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from PEM string to X509 CRL failed.");
  }
  return std::make_unique<CrlImpl>(crl);
}

StaticCrlProvider::StaticCrlProvider(const std::vector<std::string>& crls) {
  for (const auto& raw_crl : crls) {
    absl::StatusOr<std::unique_ptr<Crl>> result = Crl::Parse(raw_crl);
    GPR_ASSERT(result.ok());
    std::unique_ptr<Crl> crl = std::move(*result);
    crls_[crl->Issuer()] = std::move(crl);
  }
}

std::shared_ptr<Crl> StaticCrlProvider::GetCrl(
    const CertificateInfo& certificate_info) {
  auto it = crls_.find(certificate_info.GetIssuer());
  if (it == crls_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace experimental
}  // namespace grpc_core