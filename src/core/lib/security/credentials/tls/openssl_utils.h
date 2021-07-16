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

struct EVP_PKEYDeleter {
  void operator()(EVP_PKEY* pkey) const { EVP_PKEY_free(pkey); }
};
struct BIO_Deleter {
  void operator()(BIO* bio) const { BIO_free(bio); }
};
struct X509_Deleter {
  void operator()(X509* x509) const { X509_free(x509); }
};

}  // namespace grpc_core

#endif  // SRC_CORE_TSI_OPENSSL_UTILS_H_
