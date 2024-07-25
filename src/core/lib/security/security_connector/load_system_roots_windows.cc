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

#if defined(GPR_WINDOWS)

#pragma comment(lib, "crypt32")

#include <esent.h>
#include <wincrypt.h>

#include <vector>

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/useful.h"

namespace grpc_core {
namespace {

std::string Utf8Encode(const std::wstring& wstr) {
  if (wstr.empty()) return "";

  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(),
                                        NULL, 0, NULL, NULL);
  std::string str_to(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str_to[0],
                      size_needed, NULL, NULL);
  return str_to;
}

}  // namespace

grpc_slice LoadSystemRootCerts() {
  std::string bundle_string;

  // Open root certificate store.
  HANDLE root_cert_store = CertOpenSystemStoreW(NULL, L"ROOT");
  if (!root_cert_store) {
    return grpc_empty_slice();
  }

  // Load all root certificates from certificate store.
  PCCERT_CONTEXT cert = NULL;
  while ((cert = CertEnumCertificatesInStore(root_cert_store, cert)) != NULL) {
    // Append each certificate in PEM format.
    DWORD size = 0;
    CryptBinaryToStringW(cert->pbCertEncoded, cert->cbCertEncoded,
                         CRYPT_STRING_BASE64HEADER, NULL, &size);
    std::vector<WCHAR> pem(size);
    CryptBinaryToStringW(cert->pbCertEncoded, cert->cbCertEncoded,
                         CRYPT_STRING_BASE64HEADER, pem.data(), &size);
    bundle_string += Utf8Encode(pem.data());
  }

  CertCloseStore(root_cert_store, 0);
  if (bundle_string.size() == 0) {
    return grpc_empty_slice();
  }

  return grpc_slice_from_cpp_string(std::move(bundle_string));
}

}  // namespace grpc_core

#endif  // GPR_WINDOWS
