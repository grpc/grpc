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

#include "src/core/lib/security/credentials/external/external_account_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/external/aws_external_account_credentials.h"
#include "src/core/lib/security/credentials/external/file_external_account_credentials.h"
#include "src/core/lib/security/credentials/external/url_external_account_credentials.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/http_client/parser.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/uri.h"

#define EXTERNAL_ACCOUNT_CREDENTIALS_GRANT_TYPE \
  "urn:ietf:params:oauth:grant-type:token-exchange"
#define EXTERNAL_ACCOUNT_CREDENTIALS_REQUESTED_TOKEN_TYPE \
  "urn:ietf:params:oauth:token-type:access_token"
#define GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE \
  "https://www.googleapis.com/auth/cloud-platform"
#define IMPERSONATED_CRED_DEFAULT_LIFETIME_IN_SECONDS 3600  // 1 hour
#define IMPERSONATED_CRED_MIN_LIFETIME_IN_SECONDS 600       // 10 mins
#define IMPERSONATED_CRED_MAX_LIFETIME_IN_SECONDS 43200     // 12 hours

namespace grpc_core {

//
// ExternalAccountCredentials::NoOpFetchBody
//

ExternalAccountCredentials::NoOpFetchBody::NoOpFetchBody(
    grpc_event_engine::experimental::EventEngine& event_engine,
    absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done,
    absl::StatusOr<std::string> result)
    : FetchBody(std::move(on_done)) {
  event_engine.Run([self = RefAsSubclass<NoOpFetchBody>(),
                    result = std::move(result)]() mutable {
    ApplicationCallbackExecCtx application_exec_ctx;
    ExecCtx exec_ctx;
    self->Finish(std::move(result));
  });
}

//
// ExternalAccountCredentials::HttpFetchBody
//

ExternalAccountCredentials::HttpFetchBody::HttpFetchBody(
    absl::FunctionRef<OrphanablePtr<HttpRequest>(grpc_http_response*,
                                                 grpc_closure*)>
        start_http_request,
    absl::AnyInvocable<void(absl::StatusOr<std::string>)> on_done)
    : FetchBody(std::move(on_done)) {
  GRPC_CLOSURE_INIT(&on_http_response_, OnHttpResponse, this, nullptr);
  Ref().release();  // Ref held by HTTP request callback.
  http_request_ = start_http_request(&response_, &on_http_response_);
}

void ExternalAccountCredentials::HttpFetchBody::OnHttpResponse(
    void* arg, grpc_error_handle error) {
  RefCountedPtr<HttpFetchBody> self(static_cast<HttpFetchBody*>(arg));
  if (!error.ok()) {
    self->Finish(std::move(error));
    return;
  }
  absl::string_view response_body(self->response_.body,
                                  self->response_.body_length);
  if (self->response_.status != 200) {
    self->Finish(absl::UnavailableError(
        absl::StrCat("Call to HTTP server ended with status ",
                     self->response_.status, " [", response_body, "]")));
    return;
  }
  self->Finish(std::string(response_body));
}

//
// ExternalAccountCredentials::ExternalFetchRequest
//

// The token fetching flow:
// 1. Retrieve subject token - Subclass's RetrieveSubjectToken() gets called
// and the subject token is received in ExchangeToken().
// 2. Exchange token - ExchangeToken() gets called with the
// subject token from #1.
// 3. (Optional) Impersonate service account - ImpersonateServiceAccount() gets
// called with the access token of the response from #2. Get an impersonated
// access token in OnImpersonateServiceAccountInternal().
// 4. Finish token fetch - Return back the response that contains an access
// token in FinishTokenFetch().
ExternalAccountCredentials::ExternalFetchRequest::ExternalFetchRequest(
    ExternalAccountCredentials* creds, Timestamp deadline,
    absl::AnyInvocable<
        void(absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>)>
        on_done)
    : creds_(creds), deadline_(deadline), on_done_(std::move(on_done)) {
  fetch_body_ = creds_->RetrieveSubjectToken(
      deadline, [self = RefAsSubclass<ExternalFetchRequest>()](
                    absl::StatusOr<std::string> result) {
        self->ExchangeToken(std::move(result));
      });
}

void ExternalAccountCredentials::ExternalFetchRequest::Orphan() {
  {
    MutexLock lock(&mu_);
    fetch_body_.reset();
  }
  Unref();
}

namespace {

std::string UrlEncode(const absl::string_view s) {
  const char* hex = "0123456789ABCDEF";
  std::string result;
  result.reserve(s.length());
  for (auto c : s) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '!' ||
        c == '\'' || c == '(' || c == ')' || c == '*' || c == '~' || c == '.') {
      result.push_back(c);
    } else {
      result.push_back('%');
      result.push_back(hex[static_cast<unsigned char>(c) >> 4]);
      result.push_back(hex[static_cast<unsigned char>(c) & 15]);
    }
  }
  return result;
}

}  // namespace

void ExternalAccountCredentials::ExternalFetchRequest::ExchangeToken(
    absl::StatusOr<std::string> subject_token) {
  MutexLock lock(&mu_);
  if (MaybeFailLocked(subject_token.status())) return;
  // Parse URI.
  absl::StatusOr<URI> uri = URI::Parse(options().token_url);
  if (!uri.ok()) {
    return FinishTokenFetch(GRPC_ERROR_CREATE(
        absl::StrFormat("Invalid token url: %s. Error: %s", options().token_url,
                        uri.status().ToString())));
  }
  // Start HTTP request.
  fetch_body_ = MakeOrphanable<HttpFetchBody>(
      [&](grpc_http_response* response, grpc_closure* on_http_response) {
        grpc_http_request request;
        memset(&request, 0, sizeof(grpc_http_request));
        const bool add_authorization_header =
            !options().client_id.empty() && !options().client_secret.empty();
        request.hdr_count = add_authorization_header ? 3 : 2;
        auto* headers = static_cast<grpc_http_header*>(
            gpr_malloc(sizeof(grpc_http_header) * request.hdr_count));
        headers[0].key = gpr_strdup("Content-Type");
        headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
        headers[1].key = gpr_strdup("x-goog-api-client");
        headers[1].value = gpr_strdup(creds_->MetricsHeaderValue().c_str());
        if (add_authorization_header) {
          std::string raw_cred = absl::StrFormat("%s:%s", options().client_id,
                                                 options().client_secret);
          std::string str =
              absl::StrFormat("Basic %s", absl::Base64Escape(raw_cred));
          headers[2].key = gpr_strdup("Authorization");
          headers[2].value = gpr_strdup(str.c_str());
        }
        request.hdrs = headers;
        std::vector<std::string> body_parts;
        body_parts.push_back(absl::StrFormat(
            "audience=%s", UrlEncode(options().audience).c_str()));
        body_parts.push_back(absl::StrFormat(
            "grant_type=%s",
            UrlEncode(EXTERNAL_ACCOUNT_CREDENTIALS_GRANT_TYPE).c_str()));
        body_parts.push_back(absl::StrFormat(
            "requested_token_type=%s",
            UrlEncode(EXTERNAL_ACCOUNT_CREDENTIALS_REQUESTED_TOKEN_TYPE)
                .c_str()));
        body_parts.push_back(
            absl::StrFormat("subject_token_type=%s",
                            UrlEncode(options().subject_token_type).c_str()));
        body_parts.push_back(absl::StrFormat(
            "subject_token=%s", UrlEncode(*subject_token).c_str()));
        std::string scope = GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE;
        if (options().service_account_impersonation_url.empty()) {
          scope = absl::StrJoin(creds_->scopes_, " ");
        }
        body_parts.push_back(
            absl::StrFormat("scope=%s", UrlEncode(scope).c_str()));
        Json::Object additional_options_json_object;
        if (options().client_id.empty() && options().client_secret.empty()) {
          additional_options_json_object["userProject"] =
              Json::FromString(options().workforce_pool_user_project);
        }
        Json additional_options_json =
            Json::FromObject(std::move(additional_options_json_object));
        body_parts.push_back(absl::StrFormat(
            "options=%s",
            UrlEncode(JsonDump(additional_options_json)).c_str()));
        std::string body = absl::StrJoin(body_parts, "&");
        request.body = const_cast<char*>(body.c_str());
        request.body_length = body.size();
        RefCountedPtr<grpc_channel_credentials> http_request_creds;
        if (uri->scheme() == "http") {
          http_request_creds = RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create());
        } else {
          http_request_creds = CreateHttpRequestSSLCredentials();
        }
        auto http_request = HttpRequest::Post(
            std::move(*uri), /*args=*/nullptr, pollent(), &request, deadline(),
            on_http_response, response, std::move(http_request_creds));
        http_request->Start();
        request.body = nullptr;
        grpc_http_request_destroy(&request);
        return http_request;
      },
      [self = RefAsSubclass<ExternalFetchRequest>()](
          absl::StatusOr<std::string> result) {
        self->MaybeImpersonateServiceAccount(std::move(result));
      });
}

void ExternalAccountCredentials::ExternalFetchRequest::
    MaybeImpersonateServiceAccount(absl::StatusOr<std::string> response_body) {
  MutexLock lock(&mu_);
  if (MaybeFailLocked(response_body.status())) return;
  // If not doing impersonation, response_body contains oauth token.
  if (options().service_account_impersonation_url.empty()) {
    return FinishTokenFetch(std::move(response_body));
  }
  // Do impersonation.
  auto json = JsonParse(*response_body);
  if (!json.ok()) {
    FinishTokenFetch(GRPC_ERROR_CREATE(absl::StrCat(
        "Invalid token exchange response: ", json.status().ToString())));
    return;
  }
  if (json->type() != Json::Type::kObject) {
    FinishTokenFetch(GRPC_ERROR_CREATE(
        "Invalid token exchange response: JSON type is not object"));
    return;
  }
  auto it = json->object().find("access_token");
  if (it == json->object().end() || it->second.type() != Json::Type::kString) {
    FinishTokenFetch(GRPC_ERROR_CREATE(absl::StrFormat(
        "Missing or invalid access_token in %s.", *response_body)));
    return;
  }
  absl::string_view access_token = it->second.string();
  absl::StatusOr<URI> uri =
      URI::Parse(options().service_account_impersonation_url);
  if (!uri.ok()) {
    FinishTokenFetch(GRPC_ERROR_CREATE(absl::StrFormat(
        "Invalid service account impersonation url: %s. Error: %s",
        options().service_account_impersonation_url, uri.status().ToString())));
    return;
  }
  // Start HTTP request.
  fetch_body_ = MakeOrphanable<HttpFetchBody>(
      [&](grpc_http_response* response, grpc_closure* on_http_response) {
        grpc_http_request request;
        memset(&request, 0, sizeof(grpc_http_request));
        request.hdr_count = 2;
        grpc_http_header* headers = static_cast<grpc_http_header*>(
            gpr_malloc(sizeof(grpc_http_header) * request.hdr_count));
        headers[0].key = gpr_strdup("Content-Type");
        headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
        std::string str = absl::StrFormat("Bearer %s", access_token);
        headers[1].key = gpr_strdup("Authorization");
        headers[1].value = gpr_strdup(str.c_str());
        request.hdrs = headers;
        std::vector<std::string> body_members;
        std::string scope = absl::StrJoin(creds_->scopes_, " ");
        body_members.push_back(
            absl::StrFormat("scope=%s", UrlEncode(scope).c_str()));
        body_members.push_back(absl::StrFormat(
            "lifetime=%ds",
            options().service_account_impersonation.token_lifetime_seconds));
        std::string body = absl::StrJoin(body_members, "&");
        request.body = const_cast<char*>(body.c_str());
        request.body_length = body.size();
        // TODO(ctiller): Use the callers resource quota.
        RefCountedPtr<grpc_channel_credentials> http_request_creds;
        if (uri->scheme() == "http") {
          http_request_creds = RefCountedPtr<grpc_channel_credentials>(
              grpc_insecure_credentials_create());
        } else {
          http_request_creds = CreateHttpRequestSSLCredentials();
        }
        auto http_request = HttpRequest::Post(
            std::move(*uri), nullptr, pollent(), &request, deadline(),
            on_http_response, response, std::move(http_request_creds));
        http_request->Start();
        request.body = nullptr;
        grpc_http_request_destroy(&request);
        return http_request;
      },
      [self = RefAsSubclass<ExternalFetchRequest>()](
          absl::StatusOr<std::string> result) {
        self->OnImpersonateServiceAccount(std::move(result));
      });
}

void ExternalAccountCredentials::ExternalFetchRequest::
    OnImpersonateServiceAccount(absl::StatusOr<std::string> response_body) {
  MutexLock lock(&mu_);
  if (MaybeFailLocked(response_body.status())) return;
  auto json = JsonParse(*response_body);
  if (!json.ok()) {
    FinishTokenFetch(GRPC_ERROR_CREATE(
        absl::StrCat("Invalid service account impersonation response: ",
                     json.status().ToString())));
    return;
  }
  if (json->type() != Json::Type::kObject) {
    FinishTokenFetch(
        GRPC_ERROR_CREATE("Invalid service account impersonation response: "
                          "JSON type is not object"));
    return;
  }
  auto it = json->object().find("accessToken");
  if (it == json->object().end() || it->second.type() != Json::Type::kString) {
    FinishTokenFetch(GRPC_ERROR_CREATE(absl::StrFormat(
        "Missing or invalid accessToken in %s.", *response_body)));
    return;
  }
  absl::string_view access_token = it->second.string();
  it = json->object().find("expireTime");
  if (it == json->object().end() || it->second.type() != Json::Type::kString) {
    FinishTokenFetch(GRPC_ERROR_CREATE(absl::StrFormat(
        "Missing or invalid expireTime in %s.", *response_body)));
    return;
  }
  absl::string_view expire_time = it->second.string();
  absl::Time t;
  if (!absl::ParseTime(absl::RFC3339_full, expire_time, &t, nullptr)) {
    FinishTokenFetch(GRPC_ERROR_CREATE(
        "Invalid expire time of service account impersonation response."));
    return;
  }
  int64_t expire_in = (t - absl::Now()) / absl::Seconds(1);
  std::string body = absl::StrFormat(
      "{\"access_token\":\"%s\",\"expires_in\":%d,\"token_type\":\"Bearer\"}",
      access_token, expire_in);
  FinishTokenFetch(std::move(body));
}

void ExternalAccountCredentials::ExternalFetchRequest::FinishTokenFetch(
    absl::StatusOr<std::string> response_body) {
  absl::StatusOr<RefCountedPtr<Token>> result;
  if (!response_body.ok()) {
    LOG(ERROR) << "Fetch external account credentials access token: "
               << response_body.status();
    result = absl::Status(response_body.status().code(),
                          absl::StrCat("error fetching oauth2 token: ",
                                       response_body.status().message()));
  } else {
    absl::optional<Slice> token_value;
    Duration token_lifetime;
    if (grpc_oauth2_token_fetcher_credentials_parse_server_response_body(
            *response_body, &token_value, &token_lifetime) !=
        GRPC_CREDENTIALS_OK) {
      result = GRPC_ERROR_CREATE("Could not parse oauth token");
    } else {
      result = MakeRefCounted<Token>(std::move(*token_value),
                                     Timestamp::Now() + token_lifetime);
    }
  }
  creds_->event_engine().Run([on_done = std::exchange(on_done_, nullptr),
                              result = std::move(result)]() mutable {
    ApplicationCallbackExecCtx application_exec_ctx;
    ExecCtx exec_ctx;
    std::exchange(on_done, nullptr)(std::move(result));
  });
}

bool ExternalAccountCredentials::ExternalFetchRequest::MaybeFailLocked(
    absl::Status status) {
  if (!status.ok()) {
    FinishTokenFetch(std::move(status));
    return true;
  }
  if (fetch_body_ == nullptr) {  // Will be set by Orphan() on cancellation.
    FinishTokenFetch(
        absl::CancelledError("external account credentials fetch cancelled"));
    return true;
  }
  return false;
}

//
// ExternalAccountCredentials
//

namespace {

// Expression to match:
// //iam.googleapis.com/locations/[^/]+/workforcePools/[^/]+/providers/.+
bool MatchWorkforcePoolAudience(absl::string_view audience) {
  // Match "//iam.googleapis.com/locations/"
  if (!absl::ConsumePrefix(&audience, "//iam.googleapis.com")) return false;
  if (!absl::ConsumePrefix(&audience, "/locations/")) return false;
  // Match "[^/]+/workforcePools/"
  std::pair<absl::string_view, absl::string_view> workforce_pools_split_result =
      absl::StrSplit(audience, absl::MaxSplits("/workforcePools/", 1));
  if (absl::StrContains(workforce_pools_split_result.first, '/')) return false;
  // Match "[^/]+/providers/.+"
  std::pair<absl::string_view, absl::string_view> providers_split_result =
      absl::StrSplit(workforce_pools_split_result.second,
                     absl::MaxSplits("/providers/", 1));
  return !absl::StrContains(providers_split_result.first, '/');
}

}  // namespace

absl::StatusOr<RefCountedPtr<ExternalAccountCredentials>>
ExternalAccountCredentials::Create(
    const Json& json, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  Options options;
  options.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json.type() != Json::Type::kObject) {
    return GRPC_ERROR_CREATE("Invalid json to construct credentials options.");
  }
  auto it = json.object().find("type");
  if (it == json.object().end()) {
    return GRPC_ERROR_CREATE("type field not present.");
  }
  if (it->second.type() != Json::Type::kString) {
    return GRPC_ERROR_CREATE("type field must be a string.");
  }
  if (it->second.string() != GRPC_AUTH_JSON_TYPE_EXTERNAL_ACCOUNT) {
    return GRPC_ERROR_CREATE("Invalid credentials json type.");
  }
  options.type = GRPC_AUTH_JSON_TYPE_EXTERNAL_ACCOUNT;
  it = json.object().find("audience");
  if (it == json.object().end()) {
    return GRPC_ERROR_CREATE("audience field not present.");
  }
  if (it->second.type() != Json::Type::kString) {
    return GRPC_ERROR_CREATE("audience field must be a string.");
  }
  options.audience = it->second.string();
  it = json.object().find("subject_token_type");
  if (it == json.object().end()) {
    return GRPC_ERROR_CREATE("subject_token_type field not present.");
  }
  if (it->second.type() != Json::Type::kString) {
    return GRPC_ERROR_CREATE("subject_token_type field must be a string.");
  }
  options.subject_token_type = it->second.string();
  it = json.object().find("service_account_impersonation_url");
  if (it != json.object().end()) {
    options.service_account_impersonation_url = it->second.string();
  }
  it = json.object().find("token_url");
  if (it == json.object().end()) {
    return GRPC_ERROR_CREATE("token_url field not present.");
  }
  if (it->second.type() != Json::Type::kString) {
    return GRPC_ERROR_CREATE("token_url field must be a string.");
  }
  options.token_url = it->second.string();
  it = json.object().find("token_info_url");
  if (it != json.object().end()) {
    options.token_info_url = it->second.string();
  }
  it = json.object().find("credential_source");
  if (it == json.object().end()) {
    return GRPC_ERROR_CREATE("credential_source field not present.");
  }
  options.credential_source = it->second;
  it = json.object().find("quota_project_id");
  if (it != json.object().end()) {
    options.quota_project_id = it->second.string();
  }
  it = json.object().find("client_id");
  if (it != json.object().end()) {
    options.client_id = it->second.string();
  }
  it = json.object().find("client_secret");
  if (it != json.object().end()) {
    options.client_secret = it->second.string();
  }
  it = json.object().find("workforce_pool_user_project");
  if (it != json.object().end()) {
    if (MatchWorkforcePoolAudience(options.audience)) {
      options.workforce_pool_user_project = it->second.string();
    } else {
      return GRPC_ERROR_CREATE(
          "workforce_pool_user_project should not be set for non-workforce "
          "pool credentials");
    }
  }
  it = json.object().find("service_account_impersonation");
  options.service_account_impersonation.token_lifetime_seconds =
      IMPERSONATED_CRED_DEFAULT_LIFETIME_IN_SECONDS;
  if (it != json.object().end() && it->second.type() == Json::Type::kObject) {
    auto service_acc_imp_json = it->second;
    auto service_acc_imp_obj_it =
        service_acc_imp_json.object().find("token_lifetime_seconds");
    if (service_acc_imp_obj_it != service_acc_imp_json.object().end()) {
      if (!absl::SimpleAtoi(
              service_acc_imp_obj_it->second.string(),
              &options.service_account_impersonation.token_lifetime_seconds)) {
        return GRPC_ERROR_CREATE("token_lifetime_seconds must be a number");
      }
      if (options.service_account_impersonation.token_lifetime_seconds >
          IMPERSONATED_CRED_MAX_LIFETIME_IN_SECONDS) {
        return GRPC_ERROR_CREATE(
            absl::StrFormat("token_lifetime_seconds must be less than %ds",
                            IMPERSONATED_CRED_MAX_LIFETIME_IN_SECONDS));
      }
      if (options.service_account_impersonation.token_lifetime_seconds <
          IMPERSONATED_CRED_MIN_LIFETIME_IN_SECONDS) {
        return GRPC_ERROR_CREATE(
            absl::StrFormat("token_lifetime_seconds must be more than %ds",
                            IMPERSONATED_CRED_MIN_LIFETIME_IN_SECONDS));
      }
    }
  }
  RefCountedPtr<ExternalAccountCredentials> creds;
  grpc_error_handle error;
  if (options.credential_source.object().find("environment_id") !=
      options.credential_source.object().end()) {
    creds = MakeRefCounted<AwsExternalAccountCredentials>(
        std::move(options), std::move(scopes), std::move(event_engine), &error);
  } else if (options.credential_source.object().find("file") !=
             options.credential_source.object().end()) {
    creds = MakeRefCounted<FileExternalAccountCredentials>(
        std::move(options), std::move(scopes), std::move(event_engine), &error);
  } else if (options.credential_source.object().find("url") !=
             options.credential_source.object().end()) {
    creds = MakeRefCounted<UrlExternalAccountCredentials>(
        std::move(options), std::move(scopes), std::move(event_engine), &error);
  } else {
    return GRPC_ERROR_CREATE(
        "Invalid options credential source to create "
        "ExternalAccountCredentials.");
  }
  if (!error.ok()) return error;
  return creds;
}

ExternalAccountCredentials::ExternalAccountCredentials(
    Options options, std::vector<std::string> scopes,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : TokenFetcherCredentials(std::move(event_engine)),
      options_(std::move(options)) {
  if (scopes.empty()) {
    scopes.push_back(GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE);
  }
  scopes_ = std::move(scopes);
}

ExternalAccountCredentials::~ExternalAccountCredentials() {}

std::string ExternalAccountCredentials::MetricsHeaderValue() {
  return absl::StrFormat(
      "gl-cpp/unknown auth/%s google-byoid-sdk source/%s sa-impersonation/%v "
      "config-lifetime/%v",
      grpc_version_string(), CredentialSourceType(),
      !options_.service_account_impersonation_url.empty(),
      options_.service_account_impersonation.token_lifetime_seconds !=
          IMPERSONATED_CRED_DEFAULT_LIFETIME_IN_SECONDS);
}

absl::string_view ExternalAccountCredentials::CredentialSourceType() {
  return "unknown";
}

OrphanablePtr<ExternalAccountCredentials::FetchRequest>
ExternalAccountCredentials::FetchToken(
    Timestamp deadline,
    absl::AnyInvocable<void(absl::StatusOr<RefCountedPtr<Token>>)> on_done) {
  return MakeOrphanable<ExternalFetchRequest>(this, deadline,
                                              std::move(on_done));
}

}  // namespace grpc_core

grpc_call_credentials* grpc_external_account_credentials_create(
    const char* json_string, const char* scopes_string) {
  auto json = grpc_core::JsonParse(json_string);
  if (!json.ok()) {
    LOG(ERROR) << "External account credentials creation failed. Error: "
               << json.status();
    return nullptr;
  }
  std::vector<std::string> scopes = absl::StrSplit(scopes_string, ',');
  auto creds =
      grpc_core::ExternalAccountCredentials::Create(*json, std::move(scopes));
  if (!creds.ok()) {
    LOG(ERROR) << "External account credentials creation failed. Error: "
               << grpc_core::StatusToString(creds.status());
    return nullptr;
  }
  return creds->release();
}
