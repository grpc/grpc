//
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
//

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_TLS_UTILS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_TLS_UTILS_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "openssl/evp.h"
#include "openssl/bio.h"
#include "openssl/x509.h"

#include "absl/strings/string_view.h"

#include "src/core/lib/security/context/security_context.h"

namespace grpc_core {

// Matches \a subject_alternative_name with \a matcher. Returns true if there
// is a match, false otherwise.
bool VerifySubjectAlternativeName(absl::string_view subject_alternative_name,
                                  const std::string& matcher);

// Returns value for the specified property_name from auth context. Here the
// property is expected to have a single value. Returns empty if multiple values
// are found.
absl::string_view GetAuthPropertyValue(grpc_auth_context* context,
                                       const char* property_name);

// Returns values for the specified property_name from auth context. Here the
// property can have any number of values.
std::vector<absl::string_view> GetAuthPropertyArray(grpc_auth_context* context,
                                                    const char* property_name);

struct EVP_PKEYDeleter {
  void operator()(EVP_PKEY* pkey) const { EVP_PKEY_free(pkey); }
};
struct BIO_Deleter {
  void operator()(BIO* bio) const { BIO_free(bio); }
};
struct X509_Deleter {
  void operator()(X509* x509) const { X509_free(x509); }
};

using OwnedEVP_PKEY = std::unique_ptr<EVP_PKEY, EVP_PKEYDeleter>;
using OwnedBIO = std::unique_ptr<BIO, BIO_Deleter>;
using OwnedX509 = std::unique_ptr<X509, X509_Deleter>;

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_TLS_UTILS_H
