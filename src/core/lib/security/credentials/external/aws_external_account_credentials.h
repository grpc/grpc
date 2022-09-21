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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/security/credentials/external/aws_request_signer.h"
#include "src/core/lib/security/credentials/external/external_account_credentials.h"

namespace grpc_core {

class AwsExternalAccountCredentials final : public ExternalAccountCredentials {
 public:
  static RefCountedPtr<AwsExternalAccountCredentials> Create(
      Options options, std::vector<std::string> scopes, absl::Status* error);

  AwsExternalAccountCredentials(Options options,
                                std::vector<std::string> scopes,
                                absl::Status* error);

 private:
  void RetrieveSubjectToken(
      HTTPRequestContext* ctx, const Options& options,
      std::function<void(std::string, absl::Status)> cb) override;

  void RetrieveRegion();
  static void OnRetrieveRegion(void* arg, absl::Status error);
  void OnRetrieveRegionInternal(absl::Status error);

  void RetrieveImdsV2SessionToken();
  static void OnRetrieveImdsV2SessionToken(void* arg, absl::Status error);
  void OnRetrieveImdsV2SessionTokenInternal(absl::Status error);

  void RetrieveRoleName();
  static void OnRetrieveRoleName(void* arg, absl::Status error);
  void OnRetrieveRoleNameInternal(absl::Status error);

  void RetrieveSigningKeys();
  static void OnRetrieveSigningKeys(void* arg, absl::Status error);
  void OnRetrieveSigningKeysInternal(absl::Status error);

  void BuildSubjectToken();
  void FinishRetrieveSubjectToken(std::string subject_token,
                                  absl::Status error);

  void AddMetadataRequestHeaders(grpc_http_request* request);

  std::string audience_;
  OrphanablePtr<HttpRequest> http_request_;

  // Fields of credential source
  std::string region_url_;
  std::string url_;
  std::string regional_cred_verification_url_;
  std::string imdsv2_session_token_url_;

  // Information required by request signer
  std::string region_;
  std::string role_name_;
  std::string access_key_id_;
  std::string secret_access_key_;
  std::string token_;
  std::string imdsv2_session_token_;

  std::unique_ptr<AwsRequestSigner> signer_;
  std::string cred_verification_url_;

  HTTPRequestContext* ctx_ = nullptr;
  std::function<void(std::string, absl::Status)> cb_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_AWS_EXTERNAL_ACCOUNT_CREDENTIALS_H
