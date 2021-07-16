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

#ifndef SRC_CORE_TSI_OPENSSL_UTILS_H_
#define SRC_CORE_TSI_OPENSSL_UTILS_H_

#include "openssl/bio.h"
#include "openssl/evp.h"
#include "openssl/x509.h"

namespace grpc_core {

// A class for managing openssl `EVP_PKEY` structures.
class OwnedOpenSslPrivateKey {
 public:
  explicit OwnedOpenSslPrivateKey(const char* private_key, int size);

  explicit OwnedOpenSslPrivateKey(EVP_PKEY* pkey);

  ~OwnedOpenSslPrivateKey();

  EVP_PKEY* get_private_key() { return private_key_; }

 private:
  EVP_PKEY* private_key_ = nullptr;
};

// A class for managing openssl `X509` structures.
class OwnedOpenSslX509 {
 public:
  explicit OwnedOpenSslX509(const char* cert, int size);

  explicit OwnedOpenSslX509(X509* x509);

  ~OwnedOpenSslX509();

  X509* get_x509() { return x_509_; }

 private:
  X509* x_509_ = nullptr;
};

}  // namespace grpc_core

#endif  // SRC_CORE_TSI_OPENSSL_UTILS_H_
