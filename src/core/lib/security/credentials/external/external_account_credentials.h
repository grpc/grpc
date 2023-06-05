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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H
#define GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"

namespace grpc_core {

// Base external account credentials. The base class implements common logic for
// exchanging external account credentials for GCP access token to authorize
// requests to GCP APIs. The specific logic of retrieving subject token is
// implemented in subclasses.
class ExternalAccountCredentials
    : public grpc_oauth2_token_fetcher_credentials {
 public:
  struct ServiceAccountImpersonation {
    int32_t token_lifetime_seconds;
  };
  // External account credentials json interface.
  struct Options {
    std::string type;
    std::string audience;
    std::string subject_token_type;
    std::string service_account_impersonation_url;
    ServiceAccountImpersonation service_account_impersonation;
    std::string token_url;
    std::string token_info_url;
    Json credential_source;
    std::string quota_project_id;
    std::string client_id;
    std::string client_secret;
    std::string workforce_pool_user_project;
  };

  static RefCountedPtr<ExternalAccountCredentials> Create(
      const Json& json, std::vector<std::string> scopes,
      grpc_error_handle* error);

  ExternalAccountCredentials(Options options, std::vector<std::string> scopes);
  ~ExternalAccountCredentials() override;
  std::string debug_string() override;

 protected:
  // This is a helper struct to pass information between multiple callback based
  // asynchronous calls.
  struct HTTPRequestContext {
    HTTPRequestContext(grpc_polling_entity* pollent, Timestamp deadline)
        : pollent(pollent), deadline(deadline) {}
    ~HTTPRequestContext() { grpc_http_response_destroy(&response); }

    // Contextual parameters passed from
    // grpc_oauth2_token_fetcher_credentials::fetch_oauth2().
    grpc_polling_entity* pollent;
    Timestamp deadline;

    // Reusable token fetch http response and closure.
    grpc_closure closure;
    grpc_http_response response;
  };

  // Subclasses of base external account credentials need to override this
  // method to implement the specific subject token retrieval logic.
  // Once the subject token is ready, subclasses need to invoke
  // the callback function (cb) to pass the subject token (or error)
  // back.
  virtual void RetrieveSubjectToken(
      HTTPRequestContext* ctx, const Options& options,
      std::function<void(std::string, grpc_error_handle)> cb) = 0;

 private:
  // This method implements the common token fetch logic and it will be called
  // when grpc_oauth2_token_fetcher_credentials request a new access token.
  void fetch_oauth2(grpc_credentials_metadata_request* req,
                    grpc_polling_entity* pollent, grpc_iomgr_cb_func cb,
                    Timestamp deadline) override;

  void OnRetrieveSubjectTokenInternal(absl::string_view subject_token,
                                      grpc_error_handle error);

  void ExchangeToken(absl::string_view subject_token);
  static void OnExchangeToken(void* arg, grpc_error_handle error);
  void OnExchangeTokenInternal(grpc_error_handle error);

  void ImpersenateServiceAccount();
  static void OnImpersenateServiceAccount(void* arg, grpc_error_handle error);
  void OnImpersenateServiceAccountInternal(grpc_error_handle error);

  void FinishTokenFetch(grpc_error_handle error);

  Options options_;
  std::vector<std::string> scopes_;

  OrphanablePtr<HttpRequest> http_request_;
  HTTPRequestContext* ctx_ = nullptr;
  grpc_credentials_metadata_request* metadata_req_ = nullptr;
  grpc_iomgr_cb_func response_cb_ = nullptr;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_EXTERNAL_EXTERNAL_ACCOUNT_CREDENTIALS_H
