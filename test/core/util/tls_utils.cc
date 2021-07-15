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

#include "test/core/util/tls_utils.h"
#include "src/core/lib/security/credentials/tls/tls_utils.h"

#include "openssl/pem.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

namespace testing {

TmpFile::TmpFile(absl::string_view credential_data) {
  name_ = CreateTmpFileAndWriteData(credential_data);
  GPR_ASSERT(!name_.empty());
}

TmpFile::~TmpFile() { GPR_ASSERT(remove(name_.c_str()) == 0); }

void TmpFile::RewriteFile(absl::string_view credential_data) {
  // Create a new file containing new data.
  std::string new_name = CreateTmpFileAndWriteData(credential_data);
  GPR_ASSERT(!new_name.empty());
  // Remove the old file.
  GPR_ASSERT(remove(name_.c_str()) == 0);
  // Rename the new file to the original name.
  GPR_ASSERT(rename(new_name.c_str(), name_.c_str()) == 0);
}

std::string TmpFile::CreateTmpFileAndWriteData(
    absl::string_view credential_data) {
  char* name = nullptr;
  FILE* file_descriptor = gpr_tmpfile("GrpcTlsCertificateProviderTest", &name);
  GPR_ASSERT(fwrite(credential_data.data(), 1, credential_data.size(),
                    file_descriptor) == credential_data.size());
  GPR_ASSERT(fclose(file_descriptor) == 0);
  GPR_ASSERT(file_descriptor != nullptr);
  GPR_ASSERT(name != nullptr);
  std::string name_to_return = name;
  gpr_free(name);
  return name_to_return;
}

PemKeyCertPairList MakeCertKeyPairs(absl::string_view private_key,
                                    absl::string_view certs) {
  if (private_key.empty() && certs.empty()) {
    return {};
  }
  return PemKeyCertPairList{PemKeyCertPair(private_key, certs)};
}

std::string GetFileContents(const char* path) {
  grpc_slice slice = grpc_empty_slice();
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file", grpc_load_file(path, 0, &slice)));
  std::string credential = std::string(StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return credential;
}

absl::Status CheckPrivateKeyFormat(absl::string_view private_key) {
  if (private_key.empty()) {
    return absl::InvalidArgumentError("Private key string is empty.");
  }
  OwnedBIO private_key_bio(
      BIO_new_mem_buf(private_key.data(), private_key.size()));
  if (!private_key_bio) {
    return absl::InvalidArgumentError(
        "Conversion from private key string to BIO failed.");
  }
  OwnedEVP_PKEY private_evp_pkey(PEM_read_bio_PrivateKey(
      private_key_bio.get(), nullptr, nullptr, nullptr));
  if (!private_evp_pkey) {
    return absl::InvalidArgumentError("Invalid private key string.");
  }
  int pkey_type = EVP_PKEY_id(private_evp_pkey.get());
  switch (pkey_type) {
    case EVP_PKEY_NONE:
      return absl::InvalidArgumentError("Undefined key type.");
    case EVP_PKEY_RSA:
    case EVP_PKEY_RSA_PSS:
      // The cases that lead here represent currently supported key types.
      break;
    default:
      gpr_log(GPR_ERROR, "Key type currently not supported.");
  }
  return absl::OkStatus();
}

absl::Status CheckCertChainFormat(absl::string_view cert_chain) {
  if (cert_chain.empty()) {
    return absl::InvalidArgumentError("Certificate chain string is empty.");
  }
  OwnedBIO cert_chain_bio(
      BIO_new_mem_buf(cert_chain.data(), cert_chain.size()));
  OwnedX509InfoStack cert_stack(
      PEM_X509_INFO_read_bio(cert_chain_bio.get(), nullptr, nullptr, nullptr));
  int num_certs = sk_X509_INFO_num(cert_stack.get());
  const char* bad_format_string =
      "Certificate chain contains cert with bad format";
  if (num_certs == 0) {
    return absl::InvalidArgumentError(bad_format_string);
  }
  for (int i = 0; i < num_certs; i++) {
    // We don't need to free `cert_info` because its parent,`cert_stack`,
    // manages it
    X509_INFO* cert_info = sk_X509_INFO_value(cert_stack.get(), i);
    if (cert_info->x509 == nullptr) {
      return absl::InvalidArgumentError(bad_format_string);
    }
  }
  return absl::OkStatus();
}

}  // namespace testing

}  // namespace grpc_core
