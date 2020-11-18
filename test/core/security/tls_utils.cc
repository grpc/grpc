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

#include "test/core/security/tls_utils.h"

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

namespace testing {

TmpFile::TmpFile(absl::string_view credential_data) {
  CreateTmpFileAndWriteData(credential_data, &name_);
  GPR_ASSERT(name_ != nullptr);
}

void TmpFile::RewriteFile(absl::string_view credential_data) {
  // Create a new file containing new data.
  char* name = nullptr;
  CreateTmpFileAndWriteData(credential_data, &name);
  GPR_ASSERT(name != nullptr);
  // Remove the old file.
  GPR_ASSERT(remove(name_) == 0);
  // Rename the new file to the original name.
  GPR_ASSERT(rename(name, name_) == 0);
  gpr_free(name);
  GPR_ASSERT(name_ != nullptr);
}

void TmpFile::CreateTmpFileAndWriteData(absl::string_view credential_data,
                                        char** file_name_ptr) {
  FILE* file_descriptor =
      gpr_tmpfile("GrpcTlsCertificateProviderTest", file_name_ptr);
  // When calling fwrite, we have to read the credential in the size of
  // |data| minus 1, because we added one null terminator while loading. We
  // need to make sure the extra null terminator is not written in.
  GPR_ASSERT(fwrite(credential_data.data(), 1, credential_data.length() - 1,
                    file_descriptor) == credential_data.length() - 1);
  GPR_ASSERT(fclose(file_descriptor) == 0);
  GPR_ASSERT(file_descriptor != nullptr);
  GPR_ASSERT(file_name_ptr != nullptr);
}

PemKeyCertPairList MakeCertKeyPairs(const char* private_key,
                                    const char* certs) {
  if (strcmp(private_key, "") == 0 && strcmp(certs, "") == 0) {
    return {};
  }
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(private_key);
  ssl_pair->cert_chain = gpr_strdup(certs);
  PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(ssl_pair);
  return pem_key_cert_pairs;
}

std::string GetCredentialData(const char* path) {
  grpc_slice slice = grpc_empty_slice();
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file", grpc_load_file(path, 1, &slice)));
  std::string credential = std::string(StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return credential;
}

}  // namespace testing

}  // namespace grpc_core
