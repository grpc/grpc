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

#include "src/core/lib/security/security_connector/ssl_utils.h"

namespace grpc_core {

namespace testing {

class TmpFile {
 public:
  // Create a tmp file with the data written in.
  explicit TmpFile(absl::string_view credential_data);

  ~TmpFile() { gpr_free(name_); }

  const char* name() { return name_; }

  // Rewrite the new data in an atomic way.
  void RewriteFile(absl::string_view credential_data);

 private:
  void CreateTmpFileAndWriteData(absl::string_view credential_data,
                                 char** file_name_ptr);

  char* name_ = nullptr;
};

PemKeyCertPairList MakeCertKeyPairs(const char* private_key, const char* certs);

std::string GetCredentialData(const char* path);

}  // namespace testing

}  // namespace grpc_core
