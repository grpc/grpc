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

#include "openssl/bio.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/x509.h"

#include "src/core/tsi/openssl_utils.h"

namespace grpc_core {

OpenSslPKey::OpenSslPKey(const char* private_key, int size) {
  BIO* bio = BIO_new_mem_buf(private_key, size);
  private_key_ = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
}

OpenSslX509::OpenSslX509(const char* cert, int size) {
  BIO* bio = BIO_new_mem_buf(cert, size);
  x_509_ = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
}

}  // namespace grpc_core
