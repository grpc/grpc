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
  // Create a temporary file with |credential_data| written in.
  explicit TmpFile(absl::string_view credential_data);

  ~TmpFile();

  const std::string& name() { return name_; }

  // Rewrite |credential_data| to the temporary file, in an atomic way.
  void RewriteFile(absl::string_view credential_data);

 private:
  std::string CreateTmpFileAndWriteData(absl::string_view credential_data);

  std::string name_;
};

PemKeyCertPairList MakeCertKeyPairs(absl::string_view private_key,
                                    absl::string_view certs);

std::string GetFileContents(const char* path);

}  // namespace testing

}  // namespace grpc_core
