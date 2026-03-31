//
//
// Copyright 2026 gRPC authors.
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

#include "src/core/credentials/transport/tls/grpc_tls_certificate_selector.h"

#include <openssl/bio.h>
#include <openssl/bytestring.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/core/util/grpc_check.h"
#include "src/core/util/match.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::StatusOr<CertificateSelector::SelectCertResult>
CertificateSelector::CreateSelectCertResult(
    const std::vector<std::string>& cert_chain,
    std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>> private_key) {
#if defined(OPENSSL_IS_BORINGSSL)
  CertificateSelector::SelectCertResult result;
  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> raw_cert_chain;
  raw_cert_chain.reserve(cert_chain.size());
  for (absl::string_view cert : cert_chain) {
    bssl::UniquePtr<CRYPTO_BUFFER> raw_cert(CRYPTO_BUFFER_new(
        reinterpret_cast<const uint8_t*>(cert.data()), cert.size(),
        /*pool=*/nullptr));
    if (raw_cert == nullptr) {
      return absl::InvalidArgumentError("Failed to create raw cert.");
    }
    raw_cert_chain.push_back(std::move(raw_cert));
  }
  result.cert_chain = std::move(raw_cert_chain);
  absl::Status status = absl::OkStatus();
  MatchMutable(
      &private_key,
      [&](absl::string_view*) {
        // TODO(lwge): Support processing DER private key.
        status = absl::UnimplementedError("DER-format private key string is not supported.");
      },
      [&](std::shared_ptr<PrivateKeySigner>* key_signer) {
        result.private_key = *key_signer;
      });
  if (!status.ok()) {
    return status;
  }
  return result;
#else
  return absl::FailedPreconditionError(
      "Select cert result creation is not supported.");
#endif
}

absl::StatusOr<CertificateSelector::SelectCertResult>
CertificateSelector::CreateSelectCertResult(
    absl::string_view cert_chain,
    std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
        private_key) {
#if defined(OPENSSL_IS_BORINGSSL)
  CertificateSelector::SelectCertResult result;
  std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> raw_cert_chain;
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(cert_chain.data(), cert_chain.size()));
  uint8_t* cert_data = nullptr;
  long cert_len;
  while (PEM_bytes_read_bio(&cert_data, &cert_len, nullptr, PEM_STRING_X509,
                            bio.get(), nullptr, nullptr)) {
    raw_cert_chain.push_back(bssl::UniquePtr<CRYPTO_BUFFER>(
        CRYPTO_BUFFER_new(reinterpret_cast<const uint8_t*>(cert_data), cert_len,
                          /*pool=*/nullptr)));
    OPENSSL_free(cert_data);
  }
  if (raw_cert_chain.empty()) {
    return absl::InvalidArgumentError("Failed to parse cert chain");
  }
  result.cert_chain = std::move(raw_cert_chain);
  absl::Status status = absl::OkStatus();
  MatchMutable(
      &private_key,
      [&](absl::string_view* key) {
        GRPC_CHECK_LE(key->size(), static_cast<size_t>(INT_MAX));
        bssl::UniquePtr<BIO> pem(
            BIO_new_mem_buf(key->data(), static_cast<int>(key->size())));
        if (pem == nullptr) {
          status = absl::InvalidArgumentError("Failed to create pem BIO.");
          return;
        }
        bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(
            pem.get(), nullptr, nullptr, const_cast<char*>("")));
        if (pkey == nullptr) {
          status = absl::InvalidArgumentError("Failed to read private key.");
          return;
        }
        result.private_key = std::move(pkey);
      },
      [&](std::shared_ptr<PrivateKeySigner>* key_signer) {
        result.private_key = *key_signer;
      });
  if (!status.ok()) {
    return status;
  }
  return result;
#else
  return absl::FailedPreconditionError(
      "Select cert result creation is not supported");
#endif
}

}  // namespace grpc_core
