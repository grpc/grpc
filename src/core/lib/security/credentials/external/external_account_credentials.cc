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
#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/external/external_account_credentials.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "src/core/lib/http/parser.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/slice/b64.h"

#define EXTERNAL_ACCOUNT_CREDENTIALS_GRANT_TYPE \
  "urn:ietf:params:oauth:grant-type:token-exchange"
#define EXTERNAL_ACCOUNT_CREDENTIALS_REQUESTED_TOKEN_TYPE \
  "urn:ietf:params:oauth:token-type:access_token"
#define GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE \
  "https://www.googleapis.com/auth/cloud-platform"

namespace grpc_core {

ExternalAccountCredentials::ExternalAccountCredentials(
    ExternalAccountCredentialsOptions options, std::vector<std::string> scopes)
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
    grpc_httpcli_context* httpcli_context, grpc_polling_entity* pollent,
    grpc_iomgr_cb_func response_cb, grpc_millis deadline) {
  GPR_ASSERT(ctx_ == nullptr);
  ctx_ = new HTTPRequestContext(httpcli_context, pollent, deadline);
  metadata_req_ = metadata_req;
  response_cb_ = response_cb;
  auto cb = [this](std::string token, grpc_error* error) {
    OnRetrieveSubjectTokenInternal(token, error);
  };
  RetrieveSubjectToken(ctx_, options_, cb);
}

void ExternalAccountCredentials::OnRetrieveSubjectTokenInternal(
    absl::string_view subject_token, grpc_error* error) {
  if (error != GRPC_ERROR_NONE) {
    FinishTokenFetch(error);
  } else {
    ExchangeToken(subject_token);
  }
}

void ExternalAccountCredentials::ExchangeToken(
    absl::string_view subject_token) {
  grpc_uri* uri = grpc_uri_parse(options_.token_url, false);
  if (uri == nullptr) {
    FinishTokenFetch(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("Invalid token url: %s.", options_.token_url).c_str()));
    return;
  }
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = const_cast<char*>(uri->authority);
  request.http.path = gpr_strdup(uri->path);
  grpc_http_header* headers = nullptr;
  if (!options_.client_id.empty() && !options_.client_secret.empty()) {
    request.http.hdr_count = 2;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * request.http.hdr_count));
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
    request.http.hdr_count = 1;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * request.http.hdr_count));
    headers[0].key = gpr_strdup("Content-Type");
    headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
  }
  request.http.hdrs = headers;
  request.handshaker = (strcmp(uri->scheme, "https") == 0)
                           ? &grpc_httpcli_ssl
                           : &grpc_httpcli_plaintext;
  std::vector<std::string> body_parts;
  body_parts.push_back(absl::StrFormat("%s=%s", "audience", options_.audience));
  body_parts.push_back(absl::StrFormat(
      "%s=%s", "grant_type", EXTERNAL_ACCOUNT_CREDENTIALS_GRANT_TYPE));
  body_parts.push_back(
      absl::StrFormat("%s=%s", "requested_token_type",
                      EXTERNAL_ACCOUNT_CREDENTIALS_REQUESTED_TOKEN_TYPE));
  body_parts.push_back(absl::StrFormat("%s=%s", "subject_token_type",
                                       options_.subject_token_type));
  body_parts.push_back(
      absl::StrFormat("%s=%s", "subject_token", subject_token));
  std::string scope = GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE;
  if (options_.service_account_impersonation_url.empty()) {
    scope = absl::StrJoin(scopes_, " ");
  }
  body_parts.push_back(absl::StrFormat("%s=%s", "scope", scope));
  std::string body = absl::StrJoin(body_parts, "&");
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("external_account_credentials");
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  GRPC_CLOSURE_INIT(&ctx_->closure, OnExchangeToken, this, nullptr);
  grpc_httpcli_post(ctx_->httpcli_context, ctx_->pollent, resource_quota,
                    &request, body.c_str(), body.size(), ctx_->deadline,
                    &ctx_->closure, &ctx_->response);
  grpc_resource_quota_unref_internal(resource_quota);
  grpc_http_request_destroy(&request.http);
  grpc_uri_destroy(uri);
}

void ExternalAccountCredentials::OnExchangeToken(void* arg, grpc_error* error) {
  ExternalAccountCredentials* self =
      static_cast<ExternalAccountCredentials*>(arg);
  self->OnExchangeTokenInternal(GRPC_ERROR_REF(error));
}

void ExternalAccountCredentials::OnExchangeTokenInternal(grpc_error* error) {
  if (error != GRPC_ERROR_NONE) {
    FinishTokenFetch(error);
  } else {
    if (options_.service_account_impersonation_url.empty()) {
      metadata_req_->response = ctx_->response;
      metadata_req_->response.body = gpr_strdup(ctx_->response.body);
      FinishTokenFetch(GRPC_ERROR_NONE);
    } else {
      ImpersenateServiceAccount();
    }
  }
}

void ExternalAccountCredentials::ImpersenateServiceAccount() {
  grpc_error* error = GRPC_ERROR_NONE;
  absl::string_view response_body(ctx_->response.body,
                                  ctx_->response.body_length);
  Json json = Json::Parse(response_body, &error);
  if (error != GRPC_ERROR_NONE || json.type() != Json::Type::OBJECT) {
    FinishTokenFetch(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Invalid token exchange response.", &error, 1));
    GRPC_ERROR_UNREF(error);
    return;
  }
  auto it = json.object_value().find("access_token");
  if (it == json.object_value().end() ||
      it->second.type() != Json::Type::STRING) {
    FinishTokenFetch(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("Missing or invalid access_token in %s.", response_body)
            .c_str()));
    return;
  }
  std::string access_token = it->second.string_value();
  grpc_uri* uri =
      grpc_uri_parse(options_.service_account_impersonation_url, false);
  if (uri == nullptr) {
    FinishTokenFetch(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("Invalid service account impersonation url: %s.",
                        options_.service_account_impersonation_url)
            .c_str()));
    return;
  }
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = const_cast<char*>(uri->authority);
  request.http.path = gpr_strdup(uri->path);
  request.http.hdr_count = 2;
  grpc_http_header* headers = static_cast<grpc_http_header*>(
      gpr_malloc(sizeof(grpc_http_header) * request.http.hdr_count));
  headers[0].key = gpr_strdup("Content-Type");
  headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
  std::string str = absl::StrFormat("Bearer %s", access_token);
  headers[1].key = gpr_strdup("Authorization");
  headers[1].value = gpr_strdup(str.c_str());
  request.http.hdrs = headers;
  request.handshaker = (strcmp(uri->scheme, "https") == 0)
                           ? &grpc_httpcli_ssl
                           : &grpc_httpcli_plaintext;
  std::string scope = absl::StrJoin(scopes_, " ");
  std::string body = absl::StrFormat("%s=%s", "scope", scope);
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("external_account_credentials");
  grpc_http_response_destroy(&ctx_->response);
  ctx_->response = {};
  GRPC_CLOSURE_INIT(&ctx_->closure, OnImpersenateServiceAccount, this, nullptr);
  grpc_httpcli_post(ctx_->httpcli_context, ctx_->pollent, resource_quota,
                    &request, body.c_str(), body.size(), ctx_->deadline,
                    &ctx_->closure, &ctx_->response);
  grpc_resource_quota_unref_internal(resource_quota);
  grpc_http_request_destroy(&request.http);
  grpc_uri_destroy(uri);
}

void ExternalAccountCredentials::OnImpersenateServiceAccount(
    void* arg, grpc_error* error) {
  ExternalAccountCredentials* self =
      static_cast<ExternalAccountCredentials*>(arg);
  self->OnImpersenateServiceAccountInternal(GRPC_ERROR_REF(error));
}

void ExternalAccountCredentials::OnImpersenateServiceAccountInternal(
    grpc_error* error) {
  if (error != GRPC_ERROR_NONE) {
    FinishTokenFetch(error);
    return;
  }
  absl::string_view response_body(ctx_->response.body,
                                  ctx_->response.body_length);
  Json json = Json::Parse(response_body, &error);
  if (error != GRPC_ERROR_NONE || json.type() != Json::Type::OBJECT) {
    FinishTokenFetch(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Invalid service account impersonation response.", &error, 1));
    GRPC_ERROR_UNREF(error);
    return;
  }
  auto it = json.object_value().find("accessToken");
  if (it == json.object_value().end() ||
      it->second.type() != Json::Type::STRING) {
    FinishTokenFetch(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("Missing or invalid accessToken in %s.", response_body)
            .c_str()));
    return;
  }
  std::string access_token = it->second.string_value();
  it = json.object_value().find("expireTime");
  if (it == json.object_value().end() ||
      it->second.type() != Json::Type::STRING) {
    FinishTokenFetch(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("Missing or invalid expireTime in %s.", response_body)
            .c_str()));
    return;
  }
  std::string expire_time = it->second.string_value();
  absl::Time t;
  if (!absl::ParseTime(absl::RFC3339_full, expire_time, &t, nullptr)) {
    FinishTokenFetch(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid expire time of service account impersonation response."));
    return;
  }
  int expire_in = (t - absl::Now()) / absl::Seconds(1);
  std::string body = absl::StrFormat(
      "{\"access_token\":\"%s\",\"expires_in\":%d,\"token_type\":\"Bearer\"}",
      access_token, expire_in);
  metadata_req_->response = ctx_->response;
  metadata_req_->response.body = gpr_strdup(body.c_str());
  metadata_req_->response.body_length = body.length();
  FinishTokenFetch(GRPC_ERROR_NONE);
}

void ExternalAccountCredentials::FinishTokenFetch(grpc_error* error) {
  GRPC_LOG_IF_ERROR("Fetch external account credentials access token",
                    GRPC_ERROR_REF(error));
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
  GRPC_ERROR_UNREF(error);
}

}  // namespace grpc_core
