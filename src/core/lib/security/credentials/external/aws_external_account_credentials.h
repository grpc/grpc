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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_AWS_EXTERNAL_ACCOUNT_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_AWS_EXTERNAL_ACCOUNT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/external/aws_request_signer.h"
#include "src/core/lib/security/credentials/external/external_account_credentials.h"

namespace grpc_core {

class AwsExternalAccountCredentials final : public ExternalAccountCredentials {
 public:
  static RefCountedPtr<AwsExternalAccountCredentials> Create(
      Options options, std::vector<std::string> scopes,
      grpc_error_handle* error);

  AwsExternalAccountCredentials(Options options,
                                std::vector<std::string> scopes,
                                grpc_error_handle* error);

 private:
  void RetrieveSubjectToken(
      HTTPRequestContext* ctx, const Options& options,
      std::function<void(std::string, grpc_error_handle)> cb) override;

  void RetrieveRegion();
  static void OnRetrieveRegion(void* arg, grpc_error_handle error);
  void OnRetrieveRegionInternal(grpc_error_handle error);

  void RetrieveRoleName();
  static void OnRetrieveRoleName(void* arg, grpc_error_handle error);
  void OnRetrieveRoleNameInternal(grpc_error_handle error);

  void RetrieveSigningKeys();
  static void OnRetrieveSigningKeys(void* arg, grpc_error_handle error);
  void OnRetrieveSigningKeysInternal(grpc_error_handle error);

  void BuildSubjectToken();
  void FinishRetrieveSubjectToken(std::string subject_token,
                                  grpc_error_handle error);

  std::string audience_;

  // Fields of credential source
  std::string region_url_;
  std::string url_;
  std::string regional_cred_verification_url_;

  // Information required by request signer
  std::string region_;
  std::string role_name_;
  std::string access_key_id_;
  std::string secret_access_key_;
  std::string token_;

  std::unique_ptr<AwsRequestSigner> signer_;
  std::string cred_verification_url_;

  HTTPRequestContext* ctx_ = nullptr;
  std::function<void(std::string, grpc_error_handle)> cb_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_AWS_EXTERNAL_ACCOUNT_CREDENTIALS_H
