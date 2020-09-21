//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H_
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H_

#include <string>
#include <vector>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"

using grpc_core::Json;

namespace grpc_core {
namespace experimental {

struct ExternalAccountCredentialsOptions {
  std::string type;
  std::string audience;
  std::string subject_token_type;
  std::string service_account_impersonation_url;
  std::string token_url;
  std::string token_info_url;
  Json credential_source;
  std::string quota_project_id;
  std::string client_id;
  std::string client_secret;
};

struct TokenFetchContext {
  grpc_credentials_metadata_request* metadata_req;
  grpc_httpcli_context* httpcli_context;
  grpc_polling_entity* pollent;
  grpc_iomgr_cb_func response_cb;
  grpc_millis deadline;

  ExternalAccountCredentialsOptions options;
  std::vector<std::string> scopes;

  std::string subject_token;
  grpc_iomgr_cb_func retrieve_subject_token_cb;

  grpc_http_response token_exchange_response;
  grpc_http_response service_account_impersonate_response;

  grpc_http_response intermediate_response;
};

class ExternalAccountCredentials
    : public grpc_oauth2_token_fetcher_credentials {
 public:
  ExternalAccountCredentials(ExternalAccountCredentialsOptions options,
                             std::vector<std::string> scopes);
  ~ExternalAccountCredentials() override;
  std::string debug_string() override;

 protected:
  virtual void RetrieveSubjectToken(TokenFetchContext* ctx) = 0;

  ExternalAccountCredentialsOptions options_;
  std::vector<std::string> scopes_;

 private:
  void fetch_oauth2(grpc_credentials_metadata_request* req,
                    grpc_httpcli_context* httpcli_context,
                    grpc_polling_entity* pollent, grpc_iomgr_cb_func cb,
                    grpc_millis deadline) override;

  TokenFetchContext* ctx_;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H_
