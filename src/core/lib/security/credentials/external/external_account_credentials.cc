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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/external/external_account_credentials.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/http/httpcli_ssl_credentials.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/external/aws_external_account_credentials.h"
#include "src/core/lib/security/credentials/external/file_external_account_credentials.h"
#include "src/core/lib/security/credentials/external/url_external_account_credentials.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/uri/uri_parser.h"

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

namespace {

std::string UrlEncode(const absl::string_view& s) {
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

RefCountedPtr<ExternalAccountCredentials> ExternalAccountCredentials::Create(
    const Json& json, std::vector<std::string> scopes,
    grpc_error_handle* error) {
  GPR_ASSERT(error->ok());
  Options options;
  options.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json.type() != Json::Type::kObject) {
    *error =
        GRPC_ERROR_CREATE("Invalid json to construct credentials options.");
    return nullptr;
  }
  auto it = json.object().find("type");
  if (it == json.object().end()) {
    *error = GRPC_ERROR_CREATE("type field not present.");
    return nullptr;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("type field must be a string.");
    return nullptr;
  }
  if (it->second.string() != GRPC_AUTH_JSON_TYPE_EXTERNAL_ACCOUNT) {
    *error = GRPC_ERROR_CREATE("Invalid credentials json type.");
    return nullptr;
  }
  options.type = GRPC_AUTH_JSON_TYPE_EXTERNAL_ACCOUNT;
  it = json.object().find("audience");
  if (it == json.object().end()) {
    *error = GRPC_ERROR_CREATE("audience field not present.");
    return nullptr;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("audience field must be a string.");
    return nullptr;
  }
  options.audience = it->second.string();
  it = json.object().find("subject_token_type");
  if (it == json.object().end()) {
    *error = GRPC_ERROR_CREATE("subject_token_type field not present.");
    return nullptr;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("subject_token_type field must be a string.");
    return nullptr;
  }
  options.subject_token_type = it->second.string();
  it = json.object().find("service_account_impersonation_url");
  if (it != json.object().end()) {
    options.service_account_impersonation_url = it->second.string();
  }
  it = json.object().find("token_url");
  if (it == json.object().end()) {
    *error = GRPC_ERROR_CREATE("token_url field not present.");
    return nullptr;
  }
  if (it->second.type() != Json::Type::kString) {
    *error = GRPC_ERROR_CREATE("token_url field must be a string.");
    return nullptr;
  }
  options.token_url = it->second.string();
  it = json.object().find("token_info_url");
  if (it != json.object().end()) {
    options.token_info_url = it->second.string();
  }
  it = json.object().find("credential_source");
  if (it == json.object().end()) {
    *error = GRPC_ERROR_CREATE("credential_source field not present.");
    return nullptr;
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
      *error = GRPC_ERROR_CREATE(
          "workforce_pool_user_project should not be set for non-workforce "
          "pool credentials");
      return nullptr;
    }
  }
  it = json.object().find("service_account_impersonation");
  options.service_account_impersonation.token_lifetime_seconds =
      IMPERSONATED_CRED_DEFAULT_LIFETIME_IN_SECONDS;
  if (it != json.object().end() && it->second.type() == Json::Type::kObject) {
    auto service_acc_imp_json = it->second;
    auto service_acc_imp_obj_it =
        service_acc_imp_json.object().find("token_lifetime_seconds");
    if (service_acc_imp_obj_it != it->second.object().end()) {
      if (!absl::SimpleAtoi(
              service_acc_imp_obj_it->second.string().c_str(),
              &options.service_account_impersonation.token_lifetime_seconds)) {
        *error = GRPC_ERROR_CREATE("token_lifetime_seconds must be a number");
        return nullptr;
      }
      if (options.service_account_impersonation.token_lifetime_seconds >
          IMPERSONATED_CRED_MAX_LIFETIME_IN_SECONDS) {
        *error = GRPC_ERROR_CREATE(
            absl::StrFormat("token_lifetime_seconds must be less than %ds",
                            IMPERSONATED_CRED_MAX_LIFETIME_IN_SECONDS));
        return nullptr;
      }
      if (options.service_account_impersonation.token_lifetime_seconds <
          IMPERSONATED_CRED_MIN_LIFETIME_IN_SECONDS) {
        *error = GRPC_ERROR_CREATE(
            absl::StrFormat("token_lifetime_seconds must be more than %ds",
                            IMPERSONATED_CRED_MIN_LIFETIME_IN_SECONDS));
        return nullptr;
      }
    }
  }
  RefCountedPtr<ExternalAccountCredentials> creds;
  if (options.credential_source.object().find("environment_id") !=
      options.credential_source.object().end()) {
    creds = MakeRefCounted<AwsExternalAccountCredentials>(
        std::move(options), std::move(scopes), error);
  } else if (options.credential_source.object().find("file") !=
             options.credential_source.object().end()) {
    creds = MakeRefCounted<FileExternalAccountCredentials>(
        std::move(options), std::move(scopes), error);
  } else if (options.credential_source.object().find("url") !=
             options.credential_source.object().end()) {
    creds = MakeRefCounted<UrlExternalAccountCredentials>(
        std::move(options), std::move(scopes), error);
  } else {
    *error = GRPC_ERROR_CREATE(
        "Invalid options credential source to create "
        "ExternalAccountCredentials.");
  }
  if (error->ok()) {
    return creds;
  } else {
    return nullptr;
  }
}

ExternalAccountCredentials::ExternalAccountCredentials(
    Options options, std::vector<std::string> scopes)
    : options_(std::move(options)) {
  if (scopes.empty()) {
    scopes.push_back(GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE);
  }
  scopes_ = std::move(scopes);
}

ExternalAccountCredentials::~ExternalAccountCredentials() {}

std::string ExternalAccountCredentials::debug_string() {
  return absl::StrFormat("ExternalAccountCredentials{Audience:%s,%s}",
                         options_.audience,
                         grpc_oauth2_token_fetcher_credentials::debug_string());
}

// The token fetching flow:
// 1. Retrieve subject token - Subclass's RetrieveSubjectToken() gets called
// and the subject token is received in OnRetrieveSubjectTokenInternal().
// 2. Exchange token - ExchangeToken() gets called with the
// subject token from #1. Receive the response in OnExchangeTokenInternal().
// 3. (Optional) Impersonate service account - ImpersenateServiceAccount() gets
// called with the access token of the response from #2. Get an impersonated
// access token in OnImpersenateServiceAccountInternal().
// 4. Finish token fetch - Return back the response that contains an access
// token in FinishTokenFetch().
// TODO(chuanr): Avoid starting the remaining requests if the channel gets shut
// down.
void ExternalAccountCredentials::fetch_oauth2(
    grpc_credentials_metadata_request* metadata_req,
    grpc_polling_entity* pollent, grpc_iomgr_cb_func response_cb,
    Timestamp deadline) {
  GPR_ASSERT(ctx_ == nullptr);
  ctx_ = new HTTPRequestContext(pollent, deadline);
  metadata_req_ = metadata_req;
  response_cb_ = response_cb;
  auto cb = [this](std::string token, grpc_error_handle error) {
    OnRetrieveSubjectTokenInternal(token, error);
  };
  RetrieveSubjectToken(ctx_, options_, cb);
}

void ExternalAccountCredentials::OnRetrieveSubjectTokenInternal(
    absl::string_view subject_token, grpc_error_handle error) {
  if (!error.ok()) {
    FinishTokenFetch(error);
  } else {
    ExchangeToken(subject_token);
  }
}

void ExternalAccountCredentials::ExchangeToken(
    absl::string_view subject_token) {
  absl::StatusOr<URI> uri = URI::Parse(options_.token_url);
  if (!uri.ok()) {
    FinishTokenFetch(GRPC_ERROR_CREATE(
        absl::StrFormat("Invalid token url: %s. Error: %s", options_.token_url,
                        uri.status().ToString())));
    return;
  }
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  grpc_http_header* headers = nullptr;
  if (!options_.client_id.empty() && !options_.client_secret.empty()) {
    request.hdr_count = 2;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * request.hdr_count));
    headers[0].key = gpr_strdup("Content-Type");
    headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
    std::string raw_cred =
        absl::StrFormat("%s:%s", options_.client_id, options_.client_secret);
    char* encoded_cred =
        grpc_base64_encode(raw_cred.c_str(), raw_cred.length(), 0, 0);
    std::string str = absl::StrFormat("Basic %s", std::string(encoded_cred));
    headers[1].key = gpr_strdup("Authorization");
    headers[1].value = gpr_strdup(str.c_str());
    gpr_free(encoded_cred);
  } else {
    request.hdr_count = 1;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * request.hdr_count));
    headers[0].key = gpr_strdup("Content-Type");
    headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
  }
  request.hdrs = headers;
  std::vector<std::string> body_parts;
  body_parts.push_back(
      absl::StrFormat("audience=%s", UrlEncode(options_.audience).c_str()));
  body_parts.push_back(absl::StrFormat(
      "grant_type=%s",
      UrlEncode(EXTERNAL_ACCOUNT_CREDENTIALS_GRANT_TYPE).c_str()));
  body_parts.push_back(absl::StrFormat(
      "requested_token_type=%s",
      UrlEncode(EXTERNAL_ACCOUNT_CREDENTIALS_REQUESTED_TOKEN_TYPE).c_str()));
  body_parts.push_back(absl::StrFormat(
      "subject_token_type=%s", UrlEncode(options_.subject_token_type).c_str()));
  body_parts.push_back(
      absl::StrFormat("subject_token=%s", UrlEncode(subject_token).c_str()));
  std::string scope = GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE;
  if (options_.service_account_impersonation_url.empty()) {
    scope = absl::StrJoin(scopes_, " ");
  }
  body_parts.push_back(absl::StrFormat("scope=%s", UrlEncode(scope).c_str()));
  Json::Object addtional_options_json_object;
  if (options_.client_id.empty() && options_.client_secret.empty()) {
    addtional_options_json_object["userProject"] =
        Json::FromString(options_.workforce_pool_user_project);
  }
  Json addtional_options_json =
      Json::FromObject(std::move(addtional_options_json_object));
  body_parts.push_back(absl::StrFormat(
      "options=%s", UrlEncode(JsonDump(addtional_options_json)).c_str()));
  std::string body = absl::StrJoin(body_parts, "&");
  request.body = const_cast<char*>(body.c_str());
  request.body_length = body.size();
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  GRPC_CLOSURE_INIT(&ctx_->closure, OnExchangeToken, this, nullptr);
  GPR_ASSERT(http_request_ == nullptr);
  RefCountedPtr<grpc_channel_credentials> http_request_creds;
  if (uri->scheme() == "http") {
    http_request_creds = RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  } else {
    http_request_creds = CreateHttpRequestSSLCredentials();
  }
  http_request_ =
      HttpRequest::Post(std::move(*uri), nullptr /* channel args */,
                        ctx_->pollent, &request, ctx_->deadline, &ctx_->closure,
                        &ctx_->response, std::move(http_request_creds));
  http_request_->Start();
  request.body = nullptr;
  grpc_http_request_destroy(&request);
}

void ExternalAccountCredentials::OnExchangeToken(void* arg,
                                                 grpc_error_handle error) {
  ExternalAccountCredentials* self =
      static_cast<ExternalAccountCredentials*>(arg);
  self->OnExchangeTokenInternal(error);
}

void ExternalAccountCredentials::OnExchangeTokenInternal(
    grpc_error_handle error) {
  http_request_.reset();
  if (!error.ok()) {
    FinishTokenFetch(error);
  } else {
    if (options_.service_account_impersonation_url.empty()) {
      metadata_req_->response = ctx_->response;
      metadata_req_->response.body = gpr_strdup(
          std::string(ctx_->response.body, ctx_->response.body_length).c_str());
      metadata_req_->response.hdrs = static_cast<grpc_http_header*>(
          gpr_malloc(sizeof(grpc_http_header) * ctx_->response.hdr_count));
      for (size_t i = 0; i < ctx_->response.hdr_count; i++) {
        metadata_req_->response.hdrs[i].key =
            gpr_strdup(ctx_->response.hdrs[i].key);
        metadata_req_->response.hdrs[i].value =
            gpr_strdup(ctx_->response.hdrs[i].value);
      }
      FinishTokenFetch(absl::OkStatus());
    } else {
      ImpersenateServiceAccount();
    }
  }
}

void ExternalAccountCredentials::ImpersenateServiceAccount() {
  absl::string_view response_body(ctx_->response.body,
                                  ctx_->response.body_length);
  auto json = JsonParse(response_body);
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
        "Missing or invalid access_token in %s.", response_body)));
    return;
  }
  std::string access_token = it->second.string();
  absl::StatusOr<URI> uri =
      URI::Parse(options_.service_account_impersonation_url);
  if (!uri.ok()) {
    FinishTokenFetch(GRPC_ERROR_CREATE(absl::StrFormat(
        "Invalid service account impersonation url: %s. Error: %s",
        options_.service_account_impersonation_url, uri.status().ToString())));
    return;
  }
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
  std::string scope = absl::StrJoin(scopes_, " ");
  body_members.push_back(absl::StrFormat("scope=%s", UrlEncode(scope).c_str()));
  body_members.push_back(absl::StrFormat(
      "lifetime=%ds",
      options_.service_account_impersonation.token_lifetime_seconds));
  std::string body = absl::StrJoin(body_members, "&");
  request.body = const_cast<char*>(body.c_str());
  request.body_length = body.size();
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  GRPC_CLOSURE_INIT(&ctx_->closure, OnImpersenateServiceAccount, this, nullptr);
  // TODO(ctiller): Use the callers resource quota.
  GPR_ASSERT(http_request_ == nullptr);
  RefCountedPtr<grpc_channel_credentials> http_request_creds;
  if (uri->scheme() == "http") {
    http_request_creds = RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  } else {
    http_request_creds = CreateHttpRequestSSLCredentials();
  }
  http_request_ = HttpRequest::Post(
      std::move(*uri), nullptr, ctx_->pollent, &request, ctx_->deadline,
      &ctx_->closure, &ctx_->response, std::move(http_request_creds));
  http_request_->Start();
  request.body = nullptr;
  grpc_http_request_destroy(&request);
}

void ExternalAccountCredentials::OnImpersenateServiceAccount(
    void* arg, grpc_error_handle error) {
  ExternalAccountCredentials* self =
      static_cast<ExternalAccountCredentials*>(arg);
  self->OnImpersenateServiceAccountInternal(error);
}

void ExternalAccountCredentials::OnImpersenateServiceAccountInternal(
    grpc_error_handle error) {
  http_request_.reset();
  if (!error.ok()) {
    FinishTokenFetch(error);
    return;
  }
  absl::string_view response_body(ctx_->response.body,
                                  ctx_->response.body_length);
  auto json = JsonParse(response_body);
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
        "Missing or invalid accessToken in %s.", response_body)));
    return;
  }
  std::string access_token = it->second.string();
  it = json->object().find("expireTime");
  if (it == json->object().end() || it->second.type() != Json::Type::kString) {
    FinishTokenFetch(GRPC_ERROR_CREATE(absl::StrFormat(
        "Missing or invalid expireTime in %s.", response_body)));
    return;
  }
  std::string expire_time = it->second.string();
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
  metadata_req_->response = ctx_->response;
  metadata_req_->response.body = gpr_strdup(body.c_str());
  metadata_req_->response.body_length = body.length();
  metadata_req_->response.hdrs = static_cast<grpc_http_header*>(
      gpr_malloc(sizeof(grpc_http_header) * ctx_->response.hdr_count));
  for (size_t i = 0; i < ctx_->response.hdr_count; i++) {
    metadata_req_->response.hdrs[i].key =
        gpr_strdup(ctx_->response.hdrs[i].key);
    metadata_req_->response.hdrs[i].value =
        gpr_strdup(ctx_->response.hdrs[i].value);
  }
  FinishTokenFetch(absl::OkStatus());
}

void ExternalAccountCredentials::FinishTokenFetch(grpc_error_handle error) {
  GRPC_LOG_IF_ERROR("Fetch external account credentials access token", error);
  // Move object state into local variables.
  auto* cb = response_cb_;
  response_cb_ = nullptr;
  auto* metadata_req = metadata_req_;
  metadata_req_ = nullptr;
  auto* ctx = ctx_;
  ctx_ = nullptr;
  // Invoke the callback.
  cb(metadata_req, error);
  // Delete context.
  delete ctx;
}

}  // namespace grpc_core

grpc_call_credentials* grpc_external_account_credentials_create(
    const char* json_string, const char* scopes_string) {
  auto json = grpc_core::JsonParse(json_string);
  if (!json.ok()) {
    gpr_log(GPR_ERROR,
            "External account credentials creation failed. Error: %s.",
            json.status().ToString().c_str());
    return nullptr;
  }
  std::vector<std::string> scopes = absl::StrSplit(scopes_string, ',');
  grpc_error_handle error;
  auto creds = grpc_core::ExternalAccountCredentials::Create(
                   *json, std::move(scopes), &error)
                   .release();
  if (!error.ok()) {
    gpr_log(GPR_ERROR,
            "External account credentials creation failed. Error: %s.",
            grpc_core::StatusToString(error).c_str());
    return nullptr;
  }
  return creds;
}
