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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_PLUGGABLE_AUTH_EXTERNAL_ACCOUNT_CREDENTIALS_H
#define GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_PLUGGABLE_AUTH_EXTERNAL_ACCOUNT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "src/core/lib/gpr/subprocess.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/external/external_account_credentials.h"

namespace grpc_core {

class PluggableAuthExternalAccountCredentials final
    : public ExternalAccountCredentials {
 public:
  struct ExecutableResponse {
    bool success;
    int version;
    int64_t expiration_time;
    char* token_type;
    char* subject_token;
    char* error_code;
    char* error_message;
  };

  static RefCountedPtr<PluggableAuthExternalAccountCredentials> Create(
      Options options, std::vector<std::string> scopes,
      grpc_error_handle* error);

  PluggableAuthExternalAccountCredentials(Options options,
                                          std::vector<std::string> scopes,
                                          grpc_error_handle* error);

 private:
  bool RetrieveSubjectTokenFromCachedOutputFile();
  void RetrieveSubjectToken(
      HTTPRequestContext* ctx, const Options& options,
      std::function<void(std::string, grpc_error_handle)> cb) override;

  ExecutableResponse* ParseExecutableResponse(std::string executable_output,
                                              grpc_error_handle* error);
  void FinishRetrieveSubjectToken(std::string token, grpc_error_handle error);
  // Fields of credential_source.executable
  std::string command_;
  int64_t executable_timeout_ms_;
  std::string output_file_path_ = "";

  ExecutableResponse* executable_response_ = nullptr;
  gpr_subprocess* gpr_subprocess_;

  std::function<void(std::string, grpc_error_handle)> cb_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_PLUGGABLE_AUTH_EXTERNAL_ACCOUNT_CREDENTIALS_H
