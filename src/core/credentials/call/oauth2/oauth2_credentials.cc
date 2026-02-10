//
//
// Copyright 2015 gRPC authors.
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
//

#include "src/core/credentials/call/oauth2/oauth2_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/call/json_util.h"
#include "src/core/credentials/call/regional_access_boundary_fetcher.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/load_file.h"
#include "src/core/util/memory.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/uri.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

using grpc_core::Json;

//
// Auth Refresh Token.
//

int grpc_auth_refresh_token_is_valid(
    const grpc_auth_refresh_token* refresh_token) {
  return (refresh_token != nullptr) &&
         strcmp(refresh_token->type, GRPC_AUTH_JSON_TYPE_INVALID) != 0;
}

grpc_auth_refresh_token grpc_auth_refresh_token_create_from_json(
    const Json& json) {
  grpc_auth_refresh_token result;
  const char* prop_value;
  int success = 0;
  grpc_error_handle error;

  memset(&result, 0, sizeof(grpc_auth_refresh_token));
  result.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json.type() != Json::Type::kObject) {
    LOG(ERROR) << "Invalid json.";
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "type", &error);
  GRPC_LOG_IF_ERROR("Parsing refresh token", error);
  if (prop_value == nullptr ||
      strcmp(prop_value, GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER) != 0) {
    goto end;
  }
  result.type = GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER;

  if (!grpc_copy_json_string_property(json, "client_secret",
                                      &result.client_secret) ||
      !grpc_copy_json_string_property(json, "client_id", &result.client_id) ||
      !grpc_copy_json_string_property(json, "refresh_token",
                                      &result.refresh_token)) {
    goto end;
  }
  success = 1;

end:
  if (!success) grpc_auth_refresh_token_destruct(&result);
  return result;
}

grpc_auth_refresh_token grpc_auth_refresh_token_create_from_string(
    const char* json_string) {
  Json json;
  auto json_or = grpc_core::JsonParse(json_string);
  if (!json_or.ok()) {
    LOG(ERROR) << "JSON parsing failed: " << json_or.status();
  } else {
    json = std::move(*json_or);
  }
  return grpc_auth_refresh_token_create_from_json(json);
}

void grpc_auth_refresh_token_destruct(grpc_auth_refresh_token* refresh_token) {
  if (refresh_token == nullptr) return;
  refresh_token->type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (refresh_token->client_id != nullptr) {
    gpr_free(refresh_token->client_id);
    refresh_token->client_id = nullptr;
  }
  if (refresh_token->client_secret != nullptr) {
    gpr_free(refresh_token->client_secret);
    refresh_token->client_secret = nullptr;
  }
  if (refresh_token->refresh_token != nullptr) {
    gpr_free(refresh_token->refresh_token);
    refresh_token->refresh_token = nullptr;
  }
}

//
// Oauth2 Token parsing.
//

grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response_body(
    absl::string_view body, std::optional<grpc_core::Slice>* token_value,
    grpc_core::Duration* token_lifetime) {
  auto json = grpc_core::JsonParse(body);
  if (!json.ok()) {
    LOG(ERROR) << "Could not parse JSON from " << body << ": " << json.status();
    return GRPC_CREDENTIALS_ERROR;
  }
  if (json->type() != Json::Type::kObject) {
    LOG(ERROR) << "Response should be a JSON object";
    return GRPC_CREDENTIALS_ERROR;
  }
  auto it = json->object().find("access_token");
  if (it == json->object().end() || it->second.type() != Json::Type::kString) {
    LOG(ERROR) << "Missing or invalid access_token in JSON.";
    return GRPC_CREDENTIALS_ERROR;
  }
  absl::string_view access_token = it->second.string();
  it = json->object().find("token_type");
  if (it == json->object().end() || it->second.type() != Json::Type::kString) {
    LOG(ERROR) << "Missing or invalid token_type in JSON.";
    return GRPC_CREDENTIALS_ERROR;
  }
  absl::string_view token_type = it->second.string();
  it = json->object().find("expires_in");
  if (it == json->object().end() || it->second.type() != Json::Type::kNumber) {
    LOG(ERROR) << "Missing or invalid expires_in in JSON.";
    return GRPC_CREDENTIALS_ERROR;
  }
  absl::string_view expires_in = it->second.string();
  long seconds;
  if (!absl::SimpleAtoi(expires_in, &seconds)) {
    LOG(ERROR) << "Invalid expires_in in JSON.";
    return GRPC_CREDENTIALS_ERROR;
  }
  *token_lifetime = grpc_core::Duration::Seconds(seconds);
  *token_value = grpc_core::Slice::FromCopiedString(
      absl::StrCat(token_type, " ", access_token));
  return GRPC_CREDENTIALS_OK;
}

grpc_credentials_status
grpc_oauth2_token_fetcher_credentials_parse_server_response(
    const grpc_http_response* response,
    std::optional<grpc_core::Slice>* token_value,
    grpc_core::Duration* token_lifetime) {
  *token_value = std::nullopt;
  if (response == nullptr) {
    LOG(ERROR) << "Received NULL response.";
    return GRPC_CREDENTIALS_ERROR;
  }
  absl::string_view body(response->body, response->body_length);
  if (response->status != 200) {
    LOG(ERROR) << "Call to http server ended with error " << response->status
               << " [" << body << "]";
    return GRPC_CREDENTIALS_ERROR;
  }
  return grpc_oauth2_token_fetcher_credentials_parse_server_response_body(
      body, token_value, token_lifetime);
}

//
// Oauth2TokenFetcherCredentials
//

namespace grpc_core {

std::string Oauth2TokenFetcherCredentials::debug_string() {
  return "OAuth2TokenFetcherCredentials";
}

UniqueTypeName Oauth2TokenFetcherCredentials::type() const {
  static UniqueTypeName::Factory kFactory("Oauth2");
  return kFactory.Create();
}

OrphanablePtr<TokenFetcherCredentials::FetchRequest>
Oauth2TokenFetcherCredentials::FetchToken(
    Timestamp deadline,
    absl::AnyInvocable<
        void(absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>)>
        on_done) {
  return MakeOrphanable<HttpTokenFetcherCredentials::HttpFetchRequest>(
      this, deadline,
      [on_done = std::move(on_done)](
          absl::StatusOr<grpc_http_response> response) mutable {
        if (!response.ok()) {
          on_done(response.status());
          return;
        }
        // Parse oauth2 token.
        std::optional<Slice> access_token_value;
        Duration token_lifetime;
        grpc_credentials_status status =
            grpc_oauth2_token_fetcher_credentials_parse_server_response(
                &(*response), &access_token_value, &token_lifetime);
        if (status != GRPC_CREDENTIALS_OK) {
          on_done(absl::UnavailableError("error parsing oauth2 token"));
          return;
        }
        on_done(MakeRefCounted<Token>(std::move(*access_token_value),
                                      Timestamp::Now() + token_lifetime));
      });
}

}  // namespace grpc_core

//
//  Google Compute Engine credentials.
//

namespace {

class grpc_compute_engine_token_fetcher_credentials
    : public grpc_core::Oauth2TokenFetcherCredentials {
 public:
  grpc_compute_engine_token_fetcher_credentials()
      : regional_access_boundary_fetcher_(
            grpc_core::MakeRefCounted<grpc_core::RegionalAccessBoundaryFetcher>()) {}
  explicit grpc_compute_engine_token_fetcher_credentials(
      std::vector<grpc_core::URI::QueryParam> query_params)
      : query_params_(std::move(query_params)),
        regional_access_boundary_fetcher_(
            grpc_core::MakeRefCounted<grpc_core::RegionalAccessBoundaryFetcher>()) {}

  ~grpc_compute_engine_token_fetcher_credentials() override {
    if (regional_access_boundary_fetcher_ != nullptr) {
      regional_access_boundary_fetcher_->CancelPendingFetch();
    }
    grpc_core::MutexLock lock(&email_mu_);
    if (email_http_request_ != nullptr) {
      email_http_request_.reset();
    }
    grpc_http_response_destroy(&email_response_);
  }

  grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
  GetRequestMetadata(grpc_core::ClientMetadataHandle initial_metadata,
                     const grpc_call_credentials::GetRequestMetadataArgs* args) override {
    return grpc_core::TrySeq(
        FetchEmail(), [this, initial_metadata = std::move(initial_metadata),
                       args](std::string) mutable {
          return grpc_core::Map(
              grpc_core::Oauth2TokenFetcherCredentials::GetRequestMetadata(
                  std::move(initial_metadata), args),
              [this](absl::StatusOr<grpc_core::ClientMetadataHandle> new_metadata)
                  -> absl::StatusOr<grpc_core::ClientMetadataHandle> {
                if (!new_metadata.ok()) return new_metadata.status();
                std::string access_token;
                auto auth_val = (*new_metadata)->GetStringValue(
                    GRPC_AUTHORIZATION_METADATA_KEY, &access_token);
                if (!auth_val.has_value()) {
                  LOG(WARNING) << "No access token was found in the metadata for this credential " 
                              << "and therefore the lookup would fail. A lookup will not be attempted.";
                  return new_metadata;
                }
                return regional_access_boundary_fetcher_->Fetch(
                    build_regional_access_boundary_url(),
                    std::string(auth_val.value()),
                    std::move(*new_metadata));
              });
        });
  }

  std::string debug_string() override {
    return absl::StrFormat(
        "GoogleComputeEngineTokenFetcherCredentials{%s}",
        grpc_core::Oauth2TokenFetcherCredentials::debug_string());
  }

 private:
  grpc_core::RefCountedPtr<grpc_core::RegionalAccessBoundaryFetcher> regional_access_boundary_fetcher_;
  std::string build_regional_access_boundary_url() {
    grpc_core::MutexLock lock(&email_mu_);
    if (service_account_email_.empty()) {
      return "";
    }
    return absl::StrFormat(
        "https://iamcredentials.googleapis.com/v1/projects/-/serviceAccounts/"
        "%s/allowedLocations",
        service_account_email_);
  }
  
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> StartHttpRequest(
      grpc_polling_entity* pollent, grpc_core::Timestamp deadline,
      grpc_http_response* response, grpc_closure* on_complete) override {
    grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                               const_cast<char*>("Google")};
    grpc_http_request request;
    memset(&request, 0, sizeof(grpc_http_request));
    request.hdr_count = 1;
    request.hdrs = &header;
    // TODO(ctiller): Carry the memory quota in ctx and share it with the host
    // channel. This would allow us to cancel an authentication query when under
    // extreme memory pressure.
    auto uri = grpc_core::URI::Create("http", /*user_info=*/"",
                                      GRPC_COMPUTE_ENGINE_METADATA_HOST,
                                      GRPC_COMPUTE_ENGINE_METADATA_TOKEN_PATH,
                                      query_params_, "" /* fragment */);
    GRPC_CHECK(uri.ok());  // params are hardcoded
    auto http_request = grpc_core::HttpRequest::Get(
        std::move(*uri), /*args=*/nullptr, pollent, &request, deadline,
        on_complete, response,
        grpc_core::RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    http_request->Start();
    return http_request;
  }

  grpc_core::Mutex email_mu_;
  std::string service_account_email_ ABSL_GUARDED_BY(email_mu_);
  // Prevents multiple email fetches from being initiated.
  bool email_fetch_in_progress_ ABSL_GUARDED_BY(email_mu_) = false;
  // Differentiates between an empty email and an email fetch that has not
  // completed.
  bool email_fetch_completed_ ABSL_GUARDED_BY(email_mu_) = false;
  grpc_core::Waker email_waker_ ABSL_GUARDED_BY(email_mu_);
  grpc_http_response email_response_;
  grpc_closure on_email_fetch_done_;
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> email_http_request_;

  grpc_core::ArenaPromise<absl::StatusOr<std::string>> FetchEmail() {
    {
      grpc_core::MutexLock lock(&email_mu_);
      if (email_fetch_completed_) {
        return grpc_core::Immediate(service_account_email_);
      }
    }

    return [this]() -> grpc_core::Poll<absl::StatusOr<std::string>> {
      grpc_core::MutexLock lock(&email_mu_);
      if (email_fetch_completed_) {
        return service_account_email_;
      }
      if (!email_fetch_in_progress_) {
        email_fetch_in_progress_ = true;
        auto* pollent = grpc_core::MaybeGetContext<grpc_polling_entity>();
        StartEmailFetchLocked(pollent);
      }
      email_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
      return grpc_core::Pending();
    };
  }

  void StartEmailFetchLocked(grpc_polling_entity* pollent) {
    grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                               const_cast<char*>("Google")};
    grpc_http_request request;
    memset(&request, 0, sizeof(grpc_http_request));
    request.hdr_count = 1;
    request.hdrs = &header;

    auto uri = grpc_core::URI::Create(
        "http", /*user_info=*/"", GRPC_COMPUTE_ENGINE_METADATA_HOST,
        GRPC_COMPUTE_ENGINE_METADATA_EMAIL_PATH, {}, "" /* fragment */);
    GRPC_CHECK(uri.ok());

    Ref().release();  // Ref taken for callback
    grpc_http_response_destroy(&email_response_);
    email_response_ = grpc_http_response();

    GRPC_CLOSURE_INIT(&on_email_fetch_done_, OnEmailFetchDone, this,
                      grpc_schedule_on_exec_ctx);

    email_http_request_ = grpc_core::HttpRequest::Get(
        std::move(*uri), /*args=*/nullptr, pollent, &request,
        grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(60),
        &on_email_fetch_done_, &email_response_,
        grpc_core::RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    email_http_request_->Start();
  }

  static void OnEmailFetchDone(void* arg, grpc_error_handle error) {
    auto* self =
        static_cast<grpc_compute_engine_token_fetcher_credentials*>(arg);
    grpc_core::Waker waker;
    {
      grpc_core::MutexLock lock(&self->email_mu_);
      self->email_fetch_completed_ = true;
      if (error.ok() && self->email_response_.status == 200) {
        self->service_account_email_ = std::string(
            self->email_response_.body, self->email_response_.body_length);
      } else {
        LOG(ERROR) << "Failed to fetch service account email: "
                   << grpc_core::StatusToString(error) 
                   << " status_code: " << self->email_response_.status;
      }
      self->email_http_request_.reset();
      waker = std::move(self->email_waker_);
    }
    waker.Wakeup();
    self->Unref();
  }

  std::vector<grpc_core::URI::QueryParam> query_params_;
};
}  // namespace

grpc_call_credentials* grpc_google_compute_engine_credentials_create(
    grpc_google_compute_engine_credentials_options* options) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_compute_engine_credentials_create(options=" << options << ")";
  std::vector<grpc_core::URI::QueryParam> query_params;
  if (options != nullptr && options->alts_hard_bound) {
    query_params.push_back({"transport", "alts"});
  }
  return grpc_core::MakeRefCounted<
             grpc_compute_engine_token_fetcher_credentials>(
             std::move(query_params))
      .release();
}

//
// Google Refresh Token credentials.
//

grpc_google_refresh_token_credentials::grpc_google_refresh_token_credentials(
    grpc_auth_refresh_token refresh_token)
    : refresh_token_(refresh_token) {}

grpc_google_refresh_token_credentials::
    ~grpc_google_refresh_token_credentials() {
  grpc_auth_refresh_token_destruct(&refresh_token_);
}

grpc_core::OrphanablePtr<grpc_core::HttpRequest>
grpc_google_refresh_token_credentials::StartHttpRequest(
    grpc_polling_entity* pollent, grpc_core::Timestamp deadline,
    grpc_http_response* response, grpc_closure* on_complete) {
  grpc_http_header header = {
      const_cast<char*>("Content-Type"),
      const_cast<char*>("application/x-www-form-urlencoded")};
  std::string body = absl::StrFormat(
      GRPC_REFRESH_TOKEN_POST_BODY_FORMAT_STRING, refresh_token_.client_id,
      refresh_token_.client_secret, refresh_token_.refresh_token);
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  request.hdr_count = 1;
  request.hdrs = &header;
  request.body = const_cast<char*>(body.c_str());
  request.body_length = body.size();
  // TODO(ctiller): Carry the memory quota in ctx and share it with the host
  // channel. This would allow us to cancel an authentication query when under
  // extreme memory pressure.
  auto uri = grpc_core::URI::Create("https", /*user_info=*/"",
                                    GRPC_GOOGLE_OAUTH2_SERVICE_HOST,
                                    GRPC_GOOGLE_OAUTH2_SERVICE_TOKEN_PATH,
                                    {} /* query params */, "" /* fragment */);
  GRPC_CHECK(uri.ok());  // params are hardcoded
  auto http_request = grpc_core::HttpRequest::Post(
      std::move(*uri), /*args=*/nullptr, pollent, &request, deadline,
      on_complete, response, grpc_core::CreateHttpRequestSSLCredentials());
  http_request->Start();
  return http_request;
}

grpc_core::RefCountedPtr<grpc_call_credentials>
grpc_refresh_token_credentials_create_from_auth_refresh_token(
    grpc_auth_refresh_token refresh_token) {
  if (!grpc_auth_refresh_token_is_valid(&refresh_token)) {
    LOG(ERROR) << "Invalid input for refresh token credentials creation";
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_google_refresh_token_credentials>(
      refresh_token);
}

std::string grpc_google_refresh_token_credentials::debug_string() {
  return absl::StrFormat(
      "GoogleRefreshToken{ClientID:%s,%s}", refresh_token_.client_id,
      grpc_core::Oauth2TokenFetcherCredentials::debug_string());
}

grpc_core::UniqueTypeName grpc_google_refresh_token_credentials::type() const {
  static grpc_core::UniqueTypeName::Factory kFactory("GoogleRefreshToken");
  return kFactory.Create();
}

static std::string create_loggable_refresh_token(
    grpc_auth_refresh_token* token) {
  if (strcmp(token->type, GRPC_AUTH_JSON_TYPE_INVALID) == 0) {
    return "<Invalid json token>";
  }
  return absl::StrFormat(
      "{\n type: %s\n client_id: %s\n client_secret: "
      "<redacted>\n refresh_token: <redacted>\n}",
      token->type, token->client_id);
}

grpc_call_credentials* grpc_google_refresh_token_credentials_create(
    const char* json_refresh_token, void* reserved) {
  grpc_auth_refresh_token token =
      grpc_auth_refresh_token_create_from_string(json_refresh_token);
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_refresh_token_credentials_create(json_refresh_token="
      << create_loggable_refresh_token(&token) << ", reserved=" << reserved
      << ")";
  GRPC_CHECK_EQ(reserved, nullptr);
  return grpc_refresh_token_credentials_create_from_auth_refresh_token(token)
      .release();
}

//
// STS credentials.
//

namespace grpc_core {

namespace {

void MaybeAddToBody(const char* field_name, const char* field,
                    std::vector<std::string>* body) {
  if (field == nullptr || strlen(field) == 0) return;
  body->push_back(absl::StrFormat("&%s=%s", field_name, field));
}

grpc_error_handle LoadTokenFile(const char* path, grpc_slice* token) {
  auto slice = LoadFile(path, /*add_null_terminator=*/true);
  if (!slice.ok()) return slice.status();
  if (slice->empty()) {
    LOG(ERROR) << "Token file " << path << " is empty";
    return GRPC_ERROR_CREATE("Token file is empty.");
  }
  *token = slice->TakeCSlice();
  return absl::OkStatus();
}

class StsTokenFetcherCredentials : public Oauth2TokenFetcherCredentials {
 public:
  StsTokenFetcherCredentials(URI sts_url,
                             const grpc_sts_credentials_options* options)
      : sts_url_(std::move(sts_url)),
        resource_(gpr_strdup(options->resource)),
        audience_(gpr_strdup(options->audience)),
        scope_(gpr_strdup(options->scope)),
        requested_token_type_(gpr_strdup(options->requested_token_type)),
        subject_token_path_(gpr_strdup(options->subject_token_path)),
        subject_token_type_(gpr_strdup(options->subject_token_type)),
        actor_token_path_(gpr_strdup(options->actor_token_path)),
        actor_token_type_(gpr_strdup(options->actor_token_type)) {}

  std::string debug_string() override {
    return absl::StrFormat(
        "StsTokenFetcherCredentials{Path:%s,Authority:%s,%s}", sts_url_.path(),
        sts_url_.authority(), Oauth2TokenFetcherCredentials::debug_string());
  }

 private:
  OrphanablePtr<HttpRequest> StartHttpRequest(
      grpc_polling_entity* pollent, Timestamp deadline,
      grpc_http_response* response, grpc_closure* on_complete) override {
    grpc_http_request request;
    memset(&request, 0, sizeof(grpc_http_request));
    grpc_error_handle err = FillBody(&request.body, &request.body_length);
    if (!err.ok()) {
      ExecCtx::Run(DEBUG_LOCATION, on_complete, std::move(err));
      return nullptr;
    }
    grpc_http_header header = {
        const_cast<char*>("Content-Type"),
        const_cast<char*>("application/x-www-form-urlencoded")};
    request.hdr_count = 1;
    request.hdrs = &header;
    // TODO(ctiller): Carry the memory quota in ctx and share it with the host
    // channel. This would allow us to cancel an authentication query when under
    // extreme memory pressure.
    RefCountedPtr<grpc_channel_credentials> http_request_creds;
    if (sts_url_.scheme() == "http") {
      http_request_creds = RefCountedPtr<grpc_channel_credentials>(
          grpc_insecure_credentials_create());
    } else {
      http_request_creds = CreateHttpRequestSSLCredentials();
    }
    auto http_request = HttpRequest::Post(
        sts_url_, /*args=*/nullptr, pollent, &request, deadline, on_complete,
        response, std::move(http_request_creds));
    http_request->Start();
    gpr_free(request.body);
    return http_request;
  }

  grpc_error_handle FillBody(char** body, size_t* body_length) {
    *body = nullptr;
    std::vector<std::string> body_parts;
    grpc_slice subject_token = grpc_empty_slice();
    grpc_slice actor_token = grpc_empty_slice();
    grpc_error_handle err;

    auto cleanup = [&body, &body_length, &body_parts, &subject_token,
                    &actor_token, &err]() {
      if (err.ok()) {
        std::string body_str = absl::StrJoin(body_parts, "");
        *body = gpr_strdup(body_str.c_str());
        *body_length = body_str.size();
      }
      CSliceUnref(subject_token);
      CSliceUnref(actor_token);
      return err;
    };

    err = LoadTokenFile(subject_token_path_.get(), &subject_token);
    if (!err.ok()) return cleanup();
    body_parts.push_back(absl::StrFormat(
        GRPC_STS_POST_MINIMAL_BODY_FORMAT_STRING,
        reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(subject_token)),
        subject_token_type_.get()));
    MaybeAddToBody("resource", resource_.get(), &body_parts);
    MaybeAddToBody("audience", audience_.get(), &body_parts);
    MaybeAddToBody("scope", scope_.get(), &body_parts);
    MaybeAddToBody("requested_token_type", requested_token_type_.get(),
                   &body_parts);
    if ((actor_token_path_ != nullptr) && *actor_token_path_ != '\0') {
      err = LoadTokenFile(actor_token_path_.get(), &actor_token);
      if (!err.ok()) return cleanup();
      MaybeAddToBody(
          "actor_token",
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(actor_token)),
          &body_parts);
      MaybeAddToBody("actor_token_type", actor_token_type_.get(), &body_parts);
    }
    return cleanup();
  }

  URI sts_url_;
  UniquePtr<char> resource_;
  UniquePtr<char> audience_;
  UniquePtr<char> scope_;
  UniquePtr<char> requested_token_type_;
  UniquePtr<char> subject_token_path_;
  UniquePtr<char> subject_token_type_;
  UniquePtr<char> actor_token_path_;
  UniquePtr<char> actor_token_type_;
  OrphanablePtr<HttpRequest> http_request_;
};

}  // namespace

absl::StatusOr<URI> ValidateStsCredentialsOptions(
    const grpc_sts_credentials_options* options) {
  std::vector<grpc_error_handle> error_list;
  absl::StatusOr<URI> sts_url =
      URI::Parse(options->token_exchange_service_uri == nullptr
                     ? ""
                     : options->token_exchange_service_uri);
  if (!sts_url.ok()) {
    error_list.push_back(GRPC_ERROR_CREATE(
        absl::StrFormat("Invalid or missing STS endpoint URL. Error: %s",
                        sts_url.status().ToString())));
  } else if (sts_url->scheme() != "https" && sts_url->scheme() != "http") {
    error_list.push_back(
        GRPC_ERROR_CREATE("Invalid URI scheme, must be https to http."));
  }
  if (options->subject_token_path == nullptr ||
      strlen(options->subject_token_path) == 0) {
    error_list.push_back(
        GRPC_ERROR_CREATE("subject_token needs to be specified"));
  }
  if (options->subject_token_type == nullptr ||
      strlen(options->subject_token_type) == 0) {
    error_list.push_back(
        GRPC_ERROR_CREATE("subject_token_type needs to be specified"));
  }
  if (error_list.empty()) {
    return sts_url;
  }
  auto grpc_error_vec = GRPC_ERROR_CREATE_FROM_VECTOR(
      "Invalid STS Credentials Options", &error_list);
  auto retval = absl::InvalidArgumentError(StatusToString(grpc_error_vec));
  return retval;
}

}  // namespace grpc_core

grpc_call_credentials* grpc_sts_credentials_create(
    const grpc_sts_credentials_options* options, void* reserved) {
  GRPC_CHECK_EQ(reserved, nullptr);
  absl::StatusOr<grpc_core::URI> sts_url =
      grpc_core::ValidateStsCredentialsOptions(options);
  if (!sts_url.ok()) {
    LOG(ERROR) << "STS Credentials creation failed. Error: "
               << sts_url.status();
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_core::StsTokenFetcherCredentials>(
             std::move(*sts_url), options)
      .release();
}

//
// Oauth2 Access Token credentials.
//

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_access_token_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs*) {
  initial_metadata->Append(
      GRPC_AUTHORIZATION_METADATA_KEY, access_token_value_.Ref(),
      [](absl::string_view, const grpc_core::Slice&) { abort(); });
  return grpc_core::Immediate(std::move(initial_metadata));
}

grpc_core::UniqueTypeName grpc_access_token_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("AccessToken");
  return kFactory.Create();
}

grpc_access_token_credentials::grpc_access_token_credentials(
    const char* access_token)
    : access_token_value_(grpc_core::Slice::FromCopiedString(
          absl::StrCat("Bearer ", access_token))) {}

std::string grpc_access_token_credentials::debug_string() {
  return "AccessTokenCredentials{Token:present}";
}

grpc_call_credentials* grpc_access_token_credentials_create(
    const char* access_token, void* reserved) {
  GRPC_TRACE_LOG(api, INFO) << "grpc_access_token_credentials_create(access_"
                               "token=<redacted>, reserved="
                            << reserved << ")";
  GRPC_CHECK_EQ(reserved, nullptr);
  return grpc_core::MakeRefCounted<grpc_access_token_credentials>(access_token)
      .release();
}
