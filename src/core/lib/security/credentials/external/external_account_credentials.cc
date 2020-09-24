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
namespace experimental {

ExternalAccountCredentials::ExternalAccountCredentials(
    ExternalAccountCredentialsOptions options, std::vector<std::string> scopes)
    : options_(std::move(options)) {
  if (scopes.empty()) {
    scopes.push_back(GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE);
  }
  scopes_ = scopes;
  ctx_ = new TokenFetchContext;
}

ExternalAccountCredentials::~ExternalAccountCredentials() {
  grpc_http_response_destroy(&ctx_->token_exchange_response);
  grpc_http_response_destroy(&ctx_->service_account_impersonate_response);
  delete ctx_;
}

std::string ExternalAccountCredentials::debug_string() {
  return absl::StrFormat("ExternalAccountCredentials{Audience:%s,%s}",
                         options_.audience,
                         grpc_oauth2_token_fetcher_credentials::debug_string());
}

static void FinishTokenFetch(TokenFetchContext* ctx, grpc_error* error) {
  GRPC_LOG_IF_ERROR("Fetch external account credentials access token",
                    GRPC_ERROR_REF(error));
  ctx->response_cb(ctx->metadata_req, error);
}

static void OnServiceAccountImpersenate(void* arg, grpc_error* error) {
  TokenFetchContext* ctx = static_cast<TokenFetchContext*>(arg);
  if (error != GRPC_ERROR_NONE) {
    FinishTokenFetch(ctx, error);
    return;
  }
  std::string response_body =
      std::string(ctx->service_account_impersonate_response.body,
                  ctx->service_account_impersonate_response.body_length);
  Json::Object::const_iterator it;
  Json json = Json::Parse(response_body.c_str(), &error);
  std::string access_token =
      std::string(grpc_json_get_string_property(json, "accessToken", &error));
  std::string expire_time =
      std::string(grpc_json_get_string_property(json, "expireTime", &error));
  absl::Time t;
  if (!absl::ParseTime(absl::RFC3339_full, expire_time, &t, nullptr)) {
    FinishTokenFetch(ctx, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                              absl::StrFormat("Invalid expire time of service "
                                              "account impersonation response.")
                                  .c_str()));
    return;
  }
  int expire_in = (t - absl::Now()) / absl::Seconds(1);
  std::string body = absl::StrFormat(
      "{\"access_token\":\"%s\", \"expires_in\":%d, \"token_type\":\"%s\"}",
      access_token, expire_in, "Bearer");

  ctx->metadata_req->response = ctx->service_account_impersonate_response;
  ctx->metadata_req->response.body = gpr_strdup(body.c_str());
  ctx->metadata_req->response.body_length = body.length();
  FinishTokenFetch(ctx, GRPC_ERROR_NONE);
}

static void ServiceAccountImpersenate(TokenFetchContext* ctx) {
  Json::Object::const_iterator it;
  grpc_error* error = GRPC_ERROR_NONE;
  std::string response_body =
      std::string(ctx->token_exchange_response.body,
                  ctx->token_exchange_response.body_length);
  Json json = Json::Parse(response_body.c_str(), &error);
  if (error != GRPC_ERROR_NONE || json.type() != Json::Type::OBJECT) {
    FinishTokenFetch(ctx,
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                         absl::StrFormat("Invalid token exchange response %s.",
                                         response_body)
                             .c_str()));
    return;
  }
  it = json.object_value().find("access_token");
  if (it == json.object_value().end() ||
      it->second.type() != Json::Type::STRING) {
    FinishTokenFetch(
        ctx, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                 absl::StrFormat("Missing or invalid access_token in %s.",
                                 response_body)
                     .c_str()));
    return;
  }
  std::string access_token = it->second.string_value();
  grpc_uri* uri =
      grpc_uri_parse(ctx->options.service_account_impersonation_url, false);
  if (uri == nullptr) {
    FinishTokenFetch(
        ctx,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            absl::StrFormat("Invalid service account impersonation url %s.",
                            ctx->options.service_account_impersonation_url)
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
  std::string scope = absl::StrJoin(ctx->scopes, " ");
  std::string body = absl::StrFormat("%s=%s", "scope", scope);
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("external_account_credentials");
  grpc_httpcli_post(ctx->httpcli_context, ctx->pollent, resource_quota,
                    &request, body.c_str(), body.size(), ctx->deadline,
                    GRPC_CLOSURE_CREATE(OnServiceAccountImpersenate, ctx,
                                        grpc_schedule_on_exec_ctx),
                    &ctx->service_account_impersonate_response);
  grpc_resource_quota_unref_internal(resource_quota);
  grpc_http_request_destroy(&request.http);
  grpc_uri_destroy(uri);
}

static void OnTokenExchange(void* arg, grpc_error* error) {
  TokenFetchContext* ctx = static_cast<TokenFetchContext*>(arg);
  if (error != GRPC_ERROR_NONE) {
    FinishTokenFetch(ctx, error);
  } else {
    if (ctx->options.service_account_impersonation_url.empty()) {
      ctx->metadata_req->response = ctx->token_exchange_response;
      ctx->metadata_req->response.body =
          gpr_strdup(ctx->token_exchange_response.body);
      FinishTokenFetch(ctx, GRPC_ERROR_NONE);
    } else {
      ServiceAccountImpersenate(ctx);
    }
  }
}

static void TokenExchange(TokenFetchContext* ctx) {
  grpc_uri* uri = grpc_uri_parse(ctx->options.token_url, false);
  if (uri == nullptr) {
    FinishTokenFetch(
        ctx, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                 absl::StrFormat("Invalid token url %s", ctx->options.token_url)
                     .c_str()));
    return;
  }
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  request.host = const_cast<char*>(uri->authority);
  request.http.path = gpr_strdup(uri->path);
  grpc_http_header* headers = nullptr;
  if (!ctx->options.client_id.empty() && !ctx->options.client_secret.empty()) {
    request.http.hdr_count = 2;
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * request.http.hdr_count));
    headers[0].key = gpr_strdup("Content-Type");
    headers[0].value = gpr_strdup("application/x-www-form-urlencoded");
    std::string raw_cred = absl::StrFormat("%s:%s", ctx->options.client_id,
                                           ctx->options.client_secret);
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
  body_parts.push_back(
      absl::StrFormat("%s=%s", "audience", ctx->options.audience));
  body_parts.push_back(absl::StrFormat(
      "%s=%s", "grant_type", EXTERNAL_ACCOUNT_CREDENTIALS_GRANT_TYPE));
  body_parts.push_back(
      absl::StrFormat("%s=%s", "requested_token_type",
                      EXTERNAL_ACCOUNT_CREDENTIALS_REQUESTED_TOKEN_TYPE));
  body_parts.push_back(absl::StrFormat("%s=%s", "subject_token_type",
                                       ctx->options.subject_token_type));
  body_parts.push_back(
      absl::StrFormat("%s=%s", "subject_token", ctx->subject_token));
  std::string scope = GOOGLE_CLOUD_PLATFORM_DEFAULT_SCOPE;
  if (ctx->options.service_account_impersonation_url.empty()) {
    scope = absl::StrJoin(ctx->scopes, " ");
  }
  body_parts.push_back(absl::StrFormat("%s=%s", "scope", scope));
  std::string body = absl::StrJoin(body_parts, "&");
  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("external_account_credentials");
  grpc_httpcli_post(
      ctx->httpcli_context, ctx->pollent, resource_quota, &request,
      body.c_str(), body.size(), ctx->deadline,
      GRPC_CLOSURE_CREATE(OnTokenExchange, ctx, grpc_schedule_on_exec_ctx),
      &ctx->token_exchange_response);
  grpc_resource_quota_unref_internal(resource_quota);
  grpc_http_request_destroy(&request.http);
  grpc_uri_destroy(uri);
}

static void OnRetrieveSubjectToken(void* arg, grpc_error* error) {
  TokenFetchContext* ctx = static_cast<TokenFetchContext*>(arg);
  if (error != GRPC_ERROR_NONE) {
    FinishTokenFetch(ctx, error);
  } else {
    TokenExchange(ctx);
  }
}

// The token fetching flow:
// 1. Retrieve subject token - Call RetrieveSubjectToken() and get the subject
// token in OnRetrieveSubjectToken().
// 2. Token exchange - Make a token exchange call in TokenExchange() with the
// subject token in #1. Get the response in OnTokenExchange().
// 3. (Optional) Service account impersonation - Make a http call in
// ServiceAccountImpersenate() with the access token in #2. Get an impersonated
// access token in OnServiceAccountImpersenate().
// 4. Return back the response that contains an access token in
// FinishTokenFetch().
void ExternalAccountCredentials::fetch_oauth2(
    grpc_credentials_metadata_request* metadata_req,
    grpc_httpcli_context* httpcli_context, grpc_polling_entity* pollent,
    grpc_iomgr_cb_func response_cb, grpc_millis deadline) {
  ctx_->metadata_req = metadata_req;
  ctx_->httpcli_context = httpcli_context;
  ctx_->pollent = pollent;
  ctx_->response_cb = response_cb;
  ctx_->deadline = deadline;
  ctx_->options = options_;
  ctx_->scopes = scopes_;
  ctx_->subject_token = "";
  ctx_->retrieve_subject_token_cb = OnRetrieveSubjectToken;
  ctx_->token_exchange_response = {};
  ctx_->service_account_impersonate_response = {};
  RetrieveSubjectToken(ctx_);
}

}  // namespace experimental
}  // namespace grpc_core
