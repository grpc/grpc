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

#include <wincrypt.h>

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"

namespace grpc_core {
namespace {

std::string utf8Encode(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();

  int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(),
                                       NULL, 0, NULL, NULL);
  std::string strTo(sizeNeeded, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0],
                      sizeNeeded, NULL, NULL);
  return strTo;
}

}  // namespace

grpc_slice LoadSystemRootCerts() {
  std::string bundle_string;

  // Open root certificate store.
  HANDLE hRootCertStore = CertOpenSystemStoreW(NULL, L"ROOT");
  if (!hRootCertStore) {
    return grpc_empty_slice();
  }

  // Load all root certificates from certificate store.
  PCCERT_CONTEXT pCert = NULL;
  while ((pCert = CertEnumCertificatesInStore(hRootCertStore, pCert)) != NULL) {
    // Append each certificate in PEM format.
    DWORD size = 0;
    CryptBinaryToStringW(pCert->pbCertEncoded, pCert->cbCertEncoded,
                         CRYPT_STRING_BASE64HEADER, NULL, &size);
    std::vector<WCHAR> pem(size);
    CryptBinaryToStringW(pCert->pbCertEncoded, pCert->cbCertEncoded,
                         CRYPT_STRING_BASE64HEADER, pem.data(), &size);
    bundle_string += utf8Encode(pem.data());
  }

  CertCloseStore(hRootCertStore, 0);
  if (bundle_string.size() == 0) {
    return grpc_empty_slice();
  }

  char* result_bundle_string =
      static_cast<char*>(gpr_zalloc(bundle_string.size() + 1));
  strcpy(result_bundle_string, bundle_string.data());
  return grpc_slice_new(result_bundle_string, bundle_string.size() + 1,
                        gpr_free);
}

}  // namespace grpc_core

#endif  // GPR_WINDOWS
