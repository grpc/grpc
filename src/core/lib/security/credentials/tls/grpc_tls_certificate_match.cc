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

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

namespace grpc_core {

absl::StatusOr<bool> PrivateKeyAndCertificateMatch(
    absl::string_view private_key, absl::string_view cert_chain) {
  if (private_key.empty()) {
    return absl::InvalidArgumentError("Private key string is empty.");
  }
  if (cert_chain.empty()) {
    return absl::InvalidArgumentError("Certificate string is empty.");
  }
  BIO* cert_bio =
      BIO_new_mem_buf(cert_chain.data(), static_cast<int>(cert_chain.size()));
  if (cert_bio == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from certificate string to BIO failed.");
  }
  // Reads the first cert from the cert_chain which is expected to be the leaf
  // cert
  X509* x509 = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
  BIO_free(cert_bio);
  if (x509 == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from PEM string to X509 failed.");
  }
  EVP_PKEY* public_evp_pkey = X509_get_pubkey(x509);
  X509_free(x509);
  if (public_evp_pkey == nullptr) {
    return absl::InvalidArgumentError(
        "Extraction of public key from x.509 certificate failed.");
  }
  BIO* private_key_bio =
      BIO_new_mem_buf(private_key.data(), static_cast<int>(private_key.size()));
  if (private_key_bio == nullptr) {
    EVP_PKEY_free(public_evp_pkey);
    return absl::InvalidArgumentError(
        "Conversion from private key string to BIO failed.");
  }
  EVP_PKEY* private_evp_pkey =
      PEM_read_bio_PrivateKey(private_key_bio, nullptr, nullptr, nullptr);
  BIO_free(private_key_bio);
  if (private_evp_pkey == nullptr) {
    EVP_PKEY_free(public_evp_pkey);
    return absl::InvalidArgumentError(
        "Conversion from PEM string to EVP_PKEY failed.");
  }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  bool result = EVP_PKEY_cmp(private_evp_pkey, public_evp_pkey) == 1;
#else
  bool result = EVP_PKEY_eq(private_evp_pkey, public_evp_pkey) == 1;
#endif
  EVP_PKEY_free(private_evp_pkey);
  EVP_PKEY_free(public_evp_pkey);
  return result;
}

}  // namespace grpc_core
