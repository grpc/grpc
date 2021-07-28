//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_TSI_OPENSSL_UTILS_H
#define GRPC_CORE_TSI_OPENSSL_UTILS_H

#include "openssl/bio.h"
#include "openssl/evp.h"
#include "openssl/x509.h"

#include "absl/strings/string_view.h"

namespace grpc_core {

// A class for managing OpenSSL |EVP_PKEY| structures.
class OpenSslPKey {
 public:
  explicit OpenSslPKey(absl::string_view private_key);

  explicit OpenSslPKey(EVP_PKEY* pkey) { p_key_ = pkey; }

  ~OpenSslPKey() { EVP_PKEY_free(p_key_); }

  EVP_PKEY* get_p_key() { return p_key_; }

 private:
  EVP_PKEY* p_key_ = nullptr;
};

// A class for managing OpenSSL |X509| structures.
class OpenSslX509 {
 public:
  explicit OpenSslX509(absl::string_view cert);

  explicit OpenSslX509(X509* x509) { x_509_ = x509; }

  ~OpenSslX509() { X509_free(x_509_); }

  X509* get_x509() { return x_509_; }

 private:
  X509* x_509_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_TSI_OPENSSL_UTILS_H
