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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"

namespace grpc_core {
namespace experimental {

// Base external account credentials. The base class implements common logic for
// exchanging external account credentials for GCP access token to authorize
// requests to GCP APIs. The specific logic of retrieving subject token is
// implemented in each subclasses.
class ExternalAccountCredentials
    : public grpc_oauth2_token_fetcher_credentials {
 public:
  // External account credentials json interface.
  struct ExternalAccountCredentialsOptions {
    std::string type;
    std::string audience;
    std::string subject_token_type;
    std::string service_account_impersonation_url;
    std::string token_url;
    std::string token_info_url;
    grpc_core::Json credential_source;
    std::string quota_project_id;
    std::string client_id;
    std::string client_secret;
  };

  ExternalAccountCredentials(ExternalAccountCredentialsOptions options,
                             std::vector<std::string> scopes);
  ~ExternalAccountCredentials() override;
  std::string debug_string() override;

 protected:
  // This is a helper struct to pass information between multiple callback based
  // asynchronous calls.
  struct TokenFetchContext {
    TokenFetchContext(grpc_httpcli_context* httpcli_context,
                      grpc_polling_entity* pollent, grpc_millis deadline)
        : httpcli_context(httpcli_context),
          pollent(pollent),
          deadline(deadline) {
      closure = {};
      response = {};
    }
    ~TokenFetchContext() { grpc_http_response_destroy(&response); }

    // Contextual parameters passed from
    // grpc_oauth2_token_fetcher_credentials::fetch_oauth2().
    grpc_httpcli_context* httpcli_context;
    grpc_polling_entity* pollent;
    grpc_millis deadline;

    // Reusable token fetch http responses and closure.
    grpc_closure closure;
    grpc_http_response response;
  };

  // Subclasses of base external account credentials need to override this
  // method to implement the specific subject token retrieval logic.
  // After the subject token is retrieved, subclasses need to invoke
  // RetrieveSubjectTokenComplete(std::string subject_token, grpc_error* error)
  // of base external account credentials to pass the subject token (or error)
  // back.
  virtual void RetrieveSubjectToken(
      const TokenFetchContext* ctx,
      const ExternalAccountCredentialsOptions* options) = 0;
  void RetrieveSubjectTokenComplete(std::string subject_token,
                                    grpc_error* error);

  ExternalAccountCredentialsOptions options() { return options_; };

 private:
  // This method implements the common token fetch logic and it will be called
  // when grpc_oauth2_token_fetcher_credentials request a new access token.
  void fetch_oauth2(grpc_credentials_metadata_request* req,
                    grpc_httpcli_context* httpcli_context,
                    grpc_polling_entity* pollent, grpc_iomgr_cb_func cb,
                    grpc_millis deadline) override;

  void TokenExchange(std::string subject_token);
  static void OnTokenExchange(void* arg, grpc_error* error);
  void OnTokenExchangeInternal(grpc_error* error);

  void ServiceAccountImpersenate();
  static void OnServiceAccountImpersenate(void* arg, grpc_error* error);
  void OnServiceAccountImpersenateInternal(grpc_error* error);

  void FinishTokenFetch(grpc_error* error);

  ExternalAccountCredentialsOptions options_;
  std::vector<std::string> scopes_;

  TokenFetchContext* ctx_ = nullptr;
  grpc_credentials_metadata_request* metadata_req_ = nullptr;
  grpc_iomgr_cb_func response_cb_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H
