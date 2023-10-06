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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_crl_provider.h"

#include <limits.h>

#include <memory>
#include <utility>
#include <vector>

#include <openssl/bio.h>
#include <openssl/mem.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace experimental {

namespace {
std::string IssuerFromCrl(X509_CRL* crl) {
  char* buf = X509_NAME_oneline(X509_CRL_get_issuer(crl), nullptr, 0);
  std::string ret;
  if (buf != nullptr) {
    ret = buf;
  }
  OPENSSL_free(buf);
  return ret;
}

}  // namespace

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
  return CrlImpl::Create(crl);
}

absl::StatusOr<std::unique_ptr<CrlImpl>> CrlImpl::Create(X509_CRL* crl) {
  std::string issuer = IssuerFromCrl(crl);
  if (issuer.empty()) {
    return absl::InvalidArgumentError("Issuer of crl cannot be empty");
  }
  return std::make_unique<CrlImpl>(crl, issuer);
}

// Copy constructor needs to duplicate the X509_CRL* since the destructor frees
// it
CrlImpl::CrlImpl(const CrlImpl& other)
    : crl_(X509_CRL_dup(other.crl())), issuer_(other.issuer_) {}

CrlImpl::~CrlImpl() { X509_CRL_free(crl_); }

absl::StatusOr<std::shared_ptr<CrlProvider>> StaticCrlProvider::Create(
    absl::Span<const std::string> crls) {
  absl::flat_hash_map<std::string, std::shared_ptr<Crl>> crl_map;
  for (const auto& raw_crl : crls) {
    absl::StatusOr<std::unique_ptr<Crl>> result = Crl::Parse(raw_crl);
    if (!result.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parsing crl string failed with result ",
                       result.status().ToString()));
    }
    std::unique_ptr<Crl> crl = std::move(*result);
    crl_map[crl->Issuer()] = std::move(crl);
  }
  StaticCrlProvider provider = StaticCrlProvider(std::move(crl_map));
  return std::make_shared<StaticCrlProvider>(std::move(provider));
}

StaticCrlProvider::StaticCrlProvider(
    absl::flat_hash_map<std::string, std::shared_ptr<Crl>> crls)
    : crls_(crls) {}

std::shared_ptr<Crl> StaticCrlProvider::GetCrl(
    const CertificateInfo& certificate_info) {
  auto it = crls_.find(certificate_info.Issuer());
  if (it == crls_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace experimental
}  // namespace grpc_core
